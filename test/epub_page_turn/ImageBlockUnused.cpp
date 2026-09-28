#include <Epub/blocks/ImageBlock.h>

#include <cstdlib>

// Persistence tests use real image metadata methods. Pixel rendering remains
// outside this harness and must never silently become a fake success.
void ImageBlock::render(GfxRenderer&, int, int) { std::abort(); }
void ImageBlock::renderPlaceholder(GfxRenderer&, int, int) const { std::abort(); }
