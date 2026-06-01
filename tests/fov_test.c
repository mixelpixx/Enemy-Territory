#include <math.h>
#include <stdio.h>
/* Declare the helper directly to keep the test self-contained (bg_public.h
   drags in engine headers). If you prefer, include bg_public.h instead. */
void BG_CalcFovHorPlus( float fov43ref, int width, int height, float *fov_x, float *fov_y );
static int approx( float a, float b ) { return fabs( a - b ) < 0.01f; }
int main( void ) {
    float fx, fy; int fails = 0;
    BG_CalcFovHorPlus( 90.0f, 640, 480, &fx, &fy );
    if ( !approx( fx, 90.0f ) ) { printf("FAIL 4:3 fov_x=%f want 90\n", fx); fails++; }
    if ( !approx( fy, 73.7398f ) ) { printf("FAIL 4:3 fov_y=%f want 73.74\n", fy); fails++; }
    float fx10, fy10;
    BG_CalcFovHorPlus( 90.0f, 1920, 1200, &fx10, &fy10 );
    if ( !approx( fy10, 73.7398f ) ) { printf("FAIL 16:10 fov_y=%f want 73.74 (anchored)\n", fy10); fails++; }
    if ( !( fx10 > 90.0f + 0.5f ) ) { printf("FAIL 16:10 fov_x=%f want >90\n", fx10); fails++; }
    printf( fails ? "FOV TEST FAILED (%d)\n" : "FOV TEST PASSED\n", fails );
    return fails ? 1 : 0;
}
