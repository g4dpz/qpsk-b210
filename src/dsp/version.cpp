#include "dsp/version.hpp"

namespace mpsk {

const char* version_string() { return MPSK_VERSION_STRING; }

namespace dsp {
const char* layer_name() { return "dsp"; }
}  // namespace dsp

}  // namespace mpsk
