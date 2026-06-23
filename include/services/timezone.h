#pragma once

namespace services::timezone {

/**
 * Look up the current UTC offset (in seconds, already including DST) for the
 * given coordinates via timeapi.io. Returns false on any network/parse error.
 * No API key required.
 */
bool fetchUtcOffsetSeconds(double lat, double lon, long* out_offset_sec);

}  // namespace services::timezone
