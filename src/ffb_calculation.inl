// Included inside FFB after its state declarations. Production calculation only.
struct CalculationSink { void (*constant)(LONG); void (*periodic)(int, float, float); };
#include "signal_recording.inl"
// Producer reset, also used when a new local car starts an explicitly muted
// legacy session. Never opens, releases or claims a native device.
static void ResetCalculationState()
{
    sharedPrevGear = prevGear = prevCollisionFlags = 0;
    lastUpdateTick = 0; prevSpeed = smoothedLateral = 0;
    std::fill(std::begin(speedHistory), std::end(speedHistory), 0.0f);
    std::fill(std::begin(latHistory), std::end(latHistory), 0.0f);
    speedHistoryIdx = latHistoryIdx = 0;
    crashImpulseTimer = gearShiftTimer = splashTimer = warmupFrames = updateCounter = 0;
    crashImpulseForce = splashAmp = roadPhase = slipPhase = 0;
    prevConstantLevel = prevStructLevel = 0;
    diagSteerRateMin = diagSteerRateMax = 0;
    for (auto& probe : steerProbe) probe.lo = probe.hi = 0;
    if (sharedModel) sharedModel->reset();
    if (sharedShaper) sharedShaper->reset();
}
static void CalculateSignals(EVWORK_CAR* car, float roughness, DWORD waterFlag,
    const CalculationSink& sink, DWORD (WINAPI *clock)(),
    void (*sampleSurface)(EVWORK_CAR*, float&, DWORD&) = nullptr,
    bool softwareOnly = false)
{
    SignalRecording::Scope recordedFrame(car,clock);
		// Warmup: ramp force scaling from 0 to 1 over first N frames.
		// Prevents garbage telemetry on initial frames from causing force spikes.
		// Using a ramp instead of a hard cutoff avoids the problem of game state
		// flickering resetting a counter.
		float warmupScale = 1.0f;
		if (warmupFrames < WARMUP_THRESHOLD)
		{
			warmupFrames++;
			warmupScale = static_cast<float>(warmupFrames) / static_cast<float>(WARMUP_THRESHOLD);
		}

		// The post-recreation ramp-in (fade back over ~250 ms instead of stepping
		// to full torque after an effect had to be recreated) now happens inside
		// the DLL, which is the only layer that knows a recreation occurred.
		const float recreateScale = 1.0f;

		// Update rates:
		// - Constant force: every frame (60 Hz) for responsive steering feel.
		// - Periodic effect envelopes: every 4th frame (~15 Hz) -- hardware
		//   renders the waveform itself, the envelope only shapes it.
		updateCounter++;
		bool updateEnvelopes = (updateCounter % 4 == 0);

		// Read telemetry from EVWORK_CAR
		float speed = car->field_1C4;                      // Normalized speed (0.0 - ~1.0+)
		float speedNorm = std::clamp(speed, 0.0f, 1.0f);
		uint32_t stateFlags = car->field_8;                // State/collision bits
		float lateralForce1 = car->field_264;              // Lateral slide component
		float lateralForce2 = car->field_268;              // Lateral slide component (opposite sign convention)
		uint32_t curGear = car->cur_gear_208;              // Current gear number
		// UNCONFIRMED: 1D0 is read as steering position and 1D4 as its rate, but
		// the drive log says |1D0| stays under 0.011 while 1D4 reaches 0.54. Both
		// terms below are therefore suspect. Run one lap with FFBDiagnosticLog
		// and read the FFB STEERSCAN line before tuning either.
		float steer = car->field_1D0;                      // scale unverified
		float steerRate = car->field_1D4;                  // scale unverified
        SignalRecording::Inputs(speed,stateFlags,lateralForce1,lateralForce2,curGear,steer,steerRate);

        // Production samples at the original ordering point; offline supplies
        // the captured numeric result and never calls the game's surface LUT.
        if (sampleSurface) sampleSurface(car, roughness, waterFlag);
        SignalRecording::Surface(roughness,waterFlag);

		// ================================================================
		// SIGNAL CONDITIONING -- lateral slide EMA, drift depth, histories
		// ================================================================

		{
			float lateralCombined = (lateralForce1 + lateralForce2);

			// Dual-rate EMA: fast attack (0.25) for responsive corner entry,
			// faster decay (0.20) for snappy arcade feel when straightening.
			float alpha = (std::abs(lateralCombined) > std::abs(smoothedLateral)) ? 0.25f : 0.20f;
			smoothedLateral = alpha * lateralCombined + (1.0f - alpha) * smoothedLateral;
		}

		// Pre-crash lateral history: the collision response corrupts the lateral
		// signal at impact time, so crash direction reads ~8 frames back
		latHistory[latHistoryIdx % 16] = smoothedLateral;
		latHistoryIdx++;

		// Subtractive deadzone on the road-load term only (1.5, was a hard-zero
		// at 5.0 -- ~21% of signal range -- which left the wheel limp through
		// straights and gentle sweepers). The virtual spring now carries center
		// feel, so this only clips the true noise floor.
		float latDz = 0.0f;
		{
			float mag = std::abs(smoothedLateral) - Settings::FFBLateralDeadzone;
			if (mag > 0.0f)
				latDz = (smoothedLateral > 0.0f) ? mag : -mag;
		}
		float latNorm = std::clamp(latDz / 24.0f, -1.0f, 1.0f);

		// Drift depth 0..1 -- the game's slide fields ARE its drift state
		// (the Xbox vibration code uses them purely as slide detectors)
		float driftAmt = std::clamp((std::abs(smoothedLateral) - 12.0f) / 12.0f, 0.0f, 1.0f);

		// THE arcade-drift cue: the wheel LIGHTENS as grip is lost
		// (front tires unloading), instead of getting heavier as before
		float gripFactor = 1.0f - Settings::FFBGripLoss * driftAmt;

		// Track speed history for crash detection + weight transfer (sliding window)
		speedHistory[speedHistoryIdx % 8] = speed;
		speedHistoryIdx++;

		// Detect crash: compare current speed to speed 6 frames ago
		// Wall deceleration is spread across many frames, so per-frame delta is tiny.
		// A 6-frame window (~100ms) captures the full deceleration event.
		// Observed wall hit deltas: ~0.04-0.06 over 6 frames.
		if (crashImpulseTimer <= 0 && speedHistoryIdx > 6)
		{
			float oldSpeed = speedHistory[(speedHistoryIdx - 6) % 8];
			float windowDelta = oldSpeed - speed;
			if (windowDelta > 0.03f && speed > 0.1f) // 3% speed loss at speed = wall hit
			{
				// Direction from PRE-crash lateral history: the collision response
				// corrupts the instantaneous lateral signal at impact (this is why
				// steering angle was abandoned too). Push away from the wall side.
				float latPre = (latHistoryIdx > 8) ? latHistory[(latHistoryIdx - 8) % 16] : smoothedLateral;
				float impactDir = (latPre >= 0.0f) ? -1.0f : 1.0f;

				// Strong jolt that cuts through steering weight (1.5 > max steering of 1.0)
				crashImpulseForce = impactDir * 1.5f * Settings::FFBWallImpact;
				crashImpulseTimer = 90; // 1.5 sec cooldown (force active first 10 frames, then lockout)
				// Reset lateral EMA so the collision physics spike doesn't sustain
				// a "pinned" steering weight force after the crash impulse ends.
				smoothedLateral = 0.0f;
				spdlog::info("FFB: CRASH impulse! windowDelta={:.3f} dir={:.0f} latPre={:.2f} force={:.2f}",
					windowDelta, impactDir, latPre, crashImpulseForce);
			}
		}

		// Also trigger on flags8 0x1000 edge (contact event)
		{
			bool collisionActive = (stateFlags & 0x1000) != 0;
			bool wasColliding = (prevCollisionFlags & 0x1000) != 0;
			if (collisionActive && !wasColliding && crashImpulseTimer <= 0)
			{
				// Same pre-crash direction logic as the speed-delta path
				float latPre = (latHistoryIdx > 8) ? latHistory[(latHistoryIdx - 8) % 16] : smoothedLateral;
				float flagDir = (latPre >= 0.0f) ? -1.0f : 1.0f;
				crashImpulseForce = flagDir * 1.2f * Settings::FFBWallImpact;
				crashImpulseTimer = 90;
				smoothedLateral = 0.0f; // Reset EMA to prevent post-crash pinning
				spdlog::info("FFB: CRASH impulse from flags8 0x1000! dir={:.0f} latPre={:.2f} force={:.2f}",
					flagDir, latPre, crashImpulseForce);
			}
		}

		// ================================================================
		// VIBRATION ENVELOPES -- computed here, rendered either on hardware
		// periodic effects (preferred) or via CF-fallback synthesis
		// ================================================================

		// Road texture: the game's own formula (roughness x speed). Asphalt has
		// roughness 0.0 -> silent (correct: smooth tarmac has no 30 Hz buzz; the
		// spring gradient carries "road connection").
		float roadAmp = roughness * speedNorm * Settings::FFBRoadTexture;
		float roadFreq = 25.0f + 12.0f * speedNorm;

		// Water splash burst at high speed on water surfaces (game's own numbers)
		if (waterFlag && roughness > 0.7f && speed > 0.95f && splashTimer <= 0)
		{
			splashAmp = (roughness - 0.7f) * speed * 0.75f;
			splashTimer = 9; // ~150ms
		}
		if (splashTimer > 0)
		{
			roadAmp = std::max(roadAmp, splashAmp);
			splashTimer--;
		}

		// Tire slip chatter: ramps in with drift depth, frequency dropping
		// 40 -> 28 Hz as the slide deepens (stick-slip period grows).
		// Shares its sine with engine idle -- the states are mutually exclusive.
		float slipAmp = 0.0f;
		float slipFreq = 40.0f;
        bool recordedIdleEntered=false;
		if (driftAmt > 0.15f && speed > 0.1f)
		{
			slipAmp = driftAmt * Settings::FFBTireSlip;
			slipFreq = 40.0f - 12.0f * driftAmt;
		}
		else if (speed < 0.05f && car->pedal_amount_34 > 0)
		{
            recordedIdleEntered=true;
			// Engine idle/launch rumble -- the only engine vibration kept.
			// Continuous at-speed engine ripple is gone: real cabinets didn't
			// render it through the steering motor, and at speed "aliveness"
			// now comes from road texture (which actually renders).
			float throttleNorm = std::clamp(static_cast<float>(car->pedal_amount_34) / 255.0f, 0.0f, 1.0f);
            SignalRecording::Throttle(throttleNorm);
			slipAmp = Settings::FFBEngineIdle * throttleNorm;
			slipFreq = 15.0f + 7.0f * throttleNorm;
		}
        SignalRecording::IdleSelection(speed,recordedIdleEntered);

		// ================================================================
		// CONSTANT FORCE -- center-out model:
		// backbone = virtual spring on steering position (what the arcade
		// cabinet's mechanical centering did) damped by the game's steering
		// derivative; lateral road load is a SECONDARY term that lightens as
		// the slide deepens; weight transfer modulates; events pulse on top.
		// ================================================================

		if (ffbLoaded && useSharedModel)
		{
			// The game's signals, handed to the shared model. Everything here is
			// already computed above by code that knows OutRun; none of it is
			// tuning, and all the tuning lives in the profile.
			dbce::force::Inputs in;
			in.steer = steer;                       in.has_steer = true;
			in.steer_rate = steerRate * 60.0f;      in.has_steer_rate = true;   // per frame -> per second
			in.speed_mps = speed * Telemetry::MaxSpeedMps;
			// latNorm is already -1..1; the profile's gReference is 1.0 so the
			// model passes it through unchanged.
			in.lateral_g = latNorm;                 in.has_lateral_g = true;
			in.drift_amount = driftAmt;             in.has_drift = true;
			if (speedHistoryIdx > 6)
			{
				in.longitudinal_g = (speed - speedHistory[(speedHistoryIdx - 6) % 8]) * 10.0f;
				in.has_longitudinal_g = true;
			}
			// Texture rides the hardware periodics when the driver has them; the
			// model's own texture term is the fallback.
			if (!periodicsActive) in.texture = std::max(roadAmp, slipAmp);
			if (crashImpulseTimer == 90)            // the tick the crash was detected
			{
				in.impact = std::min(1.0f, std::abs(crashImpulseForce));
				in.impact_direction = crashImpulseForce >= 0.0f ? 1.0f : -1.0f;
			}
			// The model owns the pulse; the producer must still expire its crash cooldown.
			if (crashImpulseTimer > 0) --crashImpulseTimer;
			in.gear_shift = (curGear != sharedPrevGear && sharedPrevGear != 0);
			sharedPrevGear = curGear;

			const float dt = 1.0f / 60.0f;
			float shaped = sharedShaper->shape(sharedModel->compute(in, dt),
				speed * Telemetry::MaxSpeedMps * 3.6f, dt, sharedModel->last_was_event);

			// FFBGlobalStrength stays the user's master dial on top of the
			// profile's own shaper.strength, and is applied inside SetConstantForce.
			LONG diMagnitude = std::clamp((LONG)(shaped * 10000.0f), (LONG)-10000, (LONG)10000);
			if (std::abs(diMagnitude - prevConstantLevel) > 15 || sharedModel->last_was_event)
			{
                sink.constant(diMagnitude);
                SignalRecording::Observe(1,0,diMagnitude,0);
            }
		}
		else if (ffbLoaded || softwareOnly)
		{
			// --- Backbone: virtual spring ---
			// speedCurve rises fast (full effect by 25% speed) then keeps growing
			// linearly -- parked wheel stays light for menus and the start line,
			// force arrives with the launch. Near-linear speed scaling matches the
			// arcade cab (speed-squared curves read as sim-like).
			float speedCurve = std::clamp(speed / 0.25f, 0.0f, 1.0f) * (0.35f + 0.65f * speedNorm);
			float F_spring = -steer * Settings::FFBSpringStrength * speedCurve;

			// --- Backbone: virtual damper on the game's own steering derivative ---
			// field_1D4 is a per-frame steering delta (small values); the scale
			// factor normalizes it into the same range as the spring term.
			// Verify observed range via FFBDiagnosticLog before fine-tuning.
			// Damper floor of 0.4 keeps a DD wheel from oscillating at low speed
			// where the spring is weak.
			constexpr float STEER_RATE_SCALE = 20.0f;
			float F_damper = -steerRate * STEER_RATE_SCALE * Settings::FFBDamperStrength * (0.4f + 0.6f * speedNorm);

			// --- Secondary: lateral road load, lightened by grip loss ---
			// In grip the wheel loads up; in a drift it goes light (gripFactor).
			// This replaces lateral-slide-as-the-whole-force, which inverted the
			// real relationship (max heaviness exactly when grip was LOST).
			float F_lat = latNorm * speedNorm * Settings::FFBSteeringWeight * gripFactor;

			// --- Weight transfer: modulates, doesn't add ---
			// Hard braking adds up to +30% weight, full throttle sheds up to 20%.
			// A multiplier cannot pull the wheel on a straight.
			float loadMod = 1.0f;
			if (speedHistoryIdx > 6)
			{
				float longAccel = (speed - speedHistory[(speedHistoryIdx - 6) % 8]) * 10.0f;
				loadMod = 1.0f + std::clamp(-longAccel * Settings::FFBWeightTransfer, -0.20f, 0.30f);
			}

			// Structural force (suppressed during the active crash jolt to
			// prevent force stacking)
			float F_struct = 0.0f;
			if (crashImpulseTimer <= 80)
				F_struct = (F_spring + F_lat) * loadMod + F_damper;

			// --- Events ---
			float F_events = 0.0f;

			// Crash impulse (time-limited jolt with long cooldown)
			// Timer starts at 90: frames 90-81 = active jolt, 80-1 = cooldown (no force, no re-trigger)
			if (crashImpulseTimer > 0)
			{
				if (crashImpulseTimer > 80) // Active jolt phase (first 10 frames = ~167ms)
				{
					float envelope;
					if (crashImpulseTimer > 85)
						envelope = 1.0f; // Full force for first ~83ms
					else
						envelope = float(crashImpulseTimer - 80) / 5.0f; // Decay over ~83ms

					F_events += crashImpulseForce * envelope;
				}
				// Frames 80-1: cooldown only, no force applied, prevents re-trigger
				crashImpulseTimer--;
			}

			// Gear shift: symmetric double pulse (+K then -K -- a "thunk").
			// A directional kick reads as "the game yanked the wheel sideways";
			// a real shift jolt is longitudinal, so the lateral pulse must net to zero.
			if (curGear != prevGear && prevGear != 0 && gearShiftTimer <= 0)
				gearShiftTimer = 6; // ~100ms at 60fps

			if (gearShiftTimer > 0)
			{
				float thunk = 0.2f * Settings::FFBGearShift * ((gearShiftTimer > 3) ? 1.0f : -1.0f);
				F_events += thunk;
				gearShiftTimer--;
			}

			float totalForce = F_struct + F_events;

			// Apply inversion if configured
			if (Settings::FFBInvertForce)
				totalForce = -totalForce;

			// Warmup ramp (garbage first frames) and post-recreation ramp-in (anti-jerk)
			totalForce *= warmupScale * recreateScale;

			// Soft saturation via tanh: preserves relative force differences
			// near the limit instead of hard-clipping to +/-1.0.
			float compressed = std::tanh(totalForce);

			// Slew-rate limiter on the STRUCTURAL force only: prevent
			// micro-oscillations on DD wheels by capping change per frame.
			// Crash impulses and gear shift pulses bypass the limiter.
			LONG structMag = (LONG)(compressed * 10000.0f);
			LONG slewDelta = structMag - prevStructLevel;
			constexpr LONG maxSlew = 600; // ~6% of 10000
			bool bypassSlew = (crashImpulseTimer > 80) || (gearShiftTimer > 0);
			if (std::abs(slewDelta) > maxSlew && !bypassSlew)
				structMag = prevStructLevel + ((slewDelta > 0) ? maxSlew : -maxSlew);
			prevStructLevel = structMag;

			// --- CF-fallback vibration (only when hardware periodics are absent) ---
			// Injected AFTER the tanh compressor so cornering load can't eat the
			// ripple (at a load of 0.6 the local tanh slope is ~0.71, at 1.0 it's
			// ~0.42 -- pre-compressor vibration lost 30-60% exactly when it
			// mattered). Synth frequencies capped at 15 Hz: zero-order-hold loss
			// at 15/60 is only ~11%, vs ~26% at 25 Hz.
			float vib = 0.0f;
			if (!periodicsActive)
			{
				if (roadAmp > 0.005f)
				{
					float f = std::min(roadFreq, 15.0f);
					roadPhase = std::fmod(roadPhase + f / 60.0f * 6.2832f, 6.2832f);
					vib += std::sin(roadPhase) * roadAmp;
				}
				else
					roadPhase = 0.0f;

				if (slipAmp > 0.005f)
				{
					float f = std::min(slipFreq, 15.0f);
					slipPhase = std::fmod(slipPhase + f / 60.0f * 6.2832f, 6.2832f);
					vib += std::sin(slipPhase) * slipAmp;
				}
				else
					slipPhase = 0.0f;
			}

			// Convert to DirectInput range: ±10000 (matching test bench)
			LONG diMagnitude = std::clamp(structMag + (LONG)(vib * 10000.0f), (LONG)-10000, (LONG)10000);

			// Deadband: skip updating if the level barely changed.
			LONG delta = std::abs(diMagnitude - prevConstantLevel);
			if (delta > 15 || crashImpulseTimer > 80)
			{
				sink.constant(diMagnitude);
                SignalRecording::Observe(1,0,diMagnitude,0);
			}
		}

		// ================================================================
		// PERIODIC CHANNEL -- hardware-rendered vibration envelopes (~15 Hz)
		// ================================================================

		if (periodicsActive && updateEnvelopes)
		{
			sink.periodic(slotRoadTexture, roadAmp, roadFreq);
            SignalRecording::Observe(2,slotRoadTexture,roadAmp,roadFreq);
			sink.periodic(slotTireSlip, slipAmp, slipFreq);
            SignalRecording::Observe(2,slotTireSlip,slipAmp,slipFreq);
		}
		// A slot whose handle dies is recreated inside the DLL, behind its own
		// 500 ms hold-off, so there is nothing to retry from here.

		// Diagnostic logging: every 2 seconds (gated behind FFBDiagnosticLog)
		if (Settings::FFBDiagnosticLog)
		{
			diagSteerRateMin = std::min(diagSteerRateMin, steerRate);
			diagSteerRateMax = std::max(diagSteerRateMax, steerRate);

			// Min/max, not an instantaneous sample: the old line sampled steer
			// once every two seconds, which is what made a signal 100x too small
			// look merely quiet.
			const float probed[] = { car->field_1C8, car->field_1CC, car->field_1D0,
			                         car->field_1D4, car->field_1DC, car->field_1E0 };
			static_assert(sizeof(probed) / sizeof(probed[0]) == sizeof(steerProbe) / sizeof(steerProbe[0]),
				"steerProbe names and probed values must line up");
			for (size_t pi = 0; pi < sizeof(probed) / sizeof(probed[0]); pi++)
			{
				steerProbe[pi].lo = std::min(steerProbe[pi].lo, probed[pi]);
				steerProbe[pi].hi = std::max(steerProbe[pi].hi, probed[pi]);
			}

			static DWORD lastDiagTime = 0;
			DWORD now = clock();
			if (now - lastDiagTime >= 2000)
			{
				lastDiagTime = now;
				spdlog::info("FFB DIAG: spd={:.3f} steer={:.3f} rate=[{:.5f}..{:.5f}] lat={:.2f} drift={:.2f} rough={:.2f} constLvl={} periodics={} warmup={}/{}",
					speed, steer, diagSteerRateMin, diagSteerRateMax, smoothedLateral, driftAmt, roughness,
					(int)prevConstantLevel, periodicsActive, warmupFrames, WARMUP_THRESHOLD);

				char scan[256];
				int at = 0;
				for (size_t pi = 0; pi < sizeof(probed) / sizeof(probed[0]) && at >= 0 && at < (int)sizeof(scan); pi++)
				{
					int wrote = snprintf(scan + at, sizeof(scan) - at, "%s[%.4f..%.4f] ",
						steerProbe[pi].name, steerProbe[pi].lo, steerProbe[pi].hi);
					if (wrote < 0) break;
					at += wrote;
					steerProbe[pi].lo = 0.0f;
					steerProbe[pi].hi = 0.0f;
				}
				spdlog::info("FFB STEERSCAN: {}", scan);

				diagSteerRateMin = 0.0f;
				diagSteerRateMax = 0.0f;
			}
		}

		// Store previous frame state for next-frame edge detection
		prevGear = curGear;
		prevCollisionFlags = stateFlags;
		prevSpeed = speed;
}
