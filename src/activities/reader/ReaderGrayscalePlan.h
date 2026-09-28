#pragma once

#include <HalDisplay.h>

// Capability selection for a page whose B/W content has already been drawn.
// Unsupported modes (including night mode) need no gray buffers or passes.
struct ReaderGrayscalePlan {
  bool text;
  bool enabled;
  bool tiled;
  bool combinedBase;
  bool overlap;

  static constexpr ReaderGrayscalePlan forPage(bool textAntiAliasing, bool hasImages,
                                               HalDisplay::GrayscaleCapabilities capabilities,
                                               bool bodyTextNeedsGrayscale = true, bool imageGrayscale = true) {
    // Image compositing retains its existing text pass and mask behavior.
    const bool text = textAntiAliasing && (hasImages || bodyTextNeedsGrayscale);
    const bool enabled = capabilities.supported() && (text || (hasImages && imageGrayscale));
    const bool tiled = enabled && capabilities.stripUploads;
    return {enabled && text, enabled, tiled,
            tiled && !hasImages && capabilities.base == HalDisplay::GrayscaleBase::Combined,
            tiled && !hasImages && capabilities.asyncBase};
  }
};
