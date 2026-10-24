#pragma once

// Host stand-in for the Bluetooth page-turner calls the reader makes around page turns and chapter builds.
namespace bleturner {
inline bool heapHeld = false;
inline int linkNoteAcks = 0;
inline bool holdsHeap() { return heapHeld; }
inline void acknowledgeLinkNote() { ++linkNoteAcks; }
inline void settleStart() {}
}  // namespace bleturner
