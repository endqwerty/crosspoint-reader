# Always-dark boot splash (r9)

The boot splash always shows a mostly black background with light artwork and
labels, regardless of the selected UI theme or inherited display polarity.
BootActivity composes the existing artwork, inverts its framebuffer in place,
and submits it with normal output polarity. This follows the existing dark
sleep-screen convention. A RenderLock covers polarity selection and painting.

The selected night/light setting is unchanged. ActivityManager applies that
setting before the next normal activity render. Silent restarts and splashless
sleep wakes retain their existing routing; the change only affects a displayed
boot splash. There is still one application-level refresh submission and no new
heap allocation, framebuffer, settings write, or background work.

Host tests compile the complete production BootActivity::onEnter method with
real renderer, logo and built-in UI font data. They cover every combination of
four orientations, two inherited polarities and both selected themes; they
check mostly-black output, visible artwork, one submission, lock lifetime and
the following screen's theme. The ActivityManager transition is represented by
its existing per-render polarity assignment, not a full RTOS task simulation.

The release is firmware-x4pro-epub-r9-final.bin, version
1.6.5rc02-x4pro-r9-6c83edd. It retains the r8 reliability fixes. No cache format
changes are needed. Reboot once in night mode and once in light mode: the splash
should remain dark and the next screen should use the selected theme. Normal
e-ink waveform transitions are hardware behavior and may still briefly flash;
these tests establish the intended image, not an electrically flash-free boot.
