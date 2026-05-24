#include "hypercube_widget.h"

#if defined(NEONDRIVE_TARGET_M5TAB5)
namespace HypercubeWidget {
static bool enabled = true;
static Activity activity = Activity::IDLE;

void begin(M5GFX& tft) { (void)tft; }
void tick() {}
void notifyScreenDrawn() {}
void setEnabled(bool en) { enabled = en; }
bool isEnabled() { return enabled; }
void setActivity(Activity a) { activity = a; }
Activity getActivity() { return activity; }
} // namespace HypercubeWidget
#endif
