# Slider input serialization

Compiles the complete production input loops, value callbacks, and result handlers for
the percent and interval selectors. Hardware routing is stubbed; the stub checks that
the shared app state is accessed while RenderLock is held. A recursive acquisition
fails the test, as does leaving the lock held after a loop return.

Coverage includes drag updates and release fallout, touch and button confirmation,
outside-tap and button cancellation, step callbacks, edge-button direction, and idle
ticks. These checks verify lock placement and callback behavior; they do not measure
touch responsiveness or reproduce FreeRTOS scheduling on hardware.
