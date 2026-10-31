#pragma once
#include <string>

// Three projected views (triple_views.cpp): the F7 correction choice (toolkit camera framing policy rev 3).
namespace TripleViews {
bool Driving();                  // a race is running: the only state with angled views
bool AngleSelected();            // the chosen correction mode is Angle ([Triple] ViewMode, Original when missing)
void SelectAngle(bool angle);    // in memory only; the caller saves [Triple] ViewMode
std::string Label();             // the actual mode now, with its step and count, for the F7 notice
bool AngleAvailable(std::string& why);   // angle correction can be drawn (hooks in, triple screens on and wide); else why not
}
