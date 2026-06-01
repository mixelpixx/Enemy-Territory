// RM: hor+ widescreen FOV helper.
//
// Treats fov43ref as the horizontal field of view tuned for a 4:3 display,
// anchors the vertical fov to that 4:3 reference, and then widens the
// horizontal fov to fill the actual (wider) aspect ratio. This keeps the
// vertical view consistent across aspect ratios (so models/world don't get
// vertically stretched) while giving widescreen displays more horizontal view
// instead of a zoomed-in "vert-" look.
//
// Pure math, no engine state: it is presentation-only and never touches the
// simulation. Kept standalone (only <math.h>) so it links into the cgame
// module and the standalone fov unit test alike.
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void BG_CalcFovHorPlus( float fov43ref, int width, int height, float *fov_x, float *fov_y ) {
	float fovy, fovx;

	// Derive the vertical fov from the 4:3 reference horizontal fov, using the
	// same square-pixel derivation the engine has always used (640x480 = 4:3).
	float x43 = 640.0f / tan( fov43ref / 360.0f * M_PI );
	fovy = atan2( 480.0f, x43 ) * 360.0f / M_PI;

	// Widen horizontal for the real aspect, keeping the anchored vertical fov.
	{
		float x = height / tan( fovy / 360.0f * M_PI );
		fovx = atan2( (float)width, x ) * 360.0f / M_PI;
	}

	*fov_x = fovx;
	*fov_y = fovy;
}
