#pragma once
#include <string>

namespace WheelSettingsUi
{
void StopFfb();
bool IsCapturing();
void FlushChanges();
void HandleShortcuts();
std::string ShortcutSummary();
}
