#pragma once
// Mock stub for esphome::http_request in GoogleTest builds.
// The real HttpRequestComponent is never used in tests — sprofiler tests
// override http_post() in MockSprofilerUpload — so only the forward
// declaration and the type are needed here.

#include <memory>
#include <string>
#include <vector>

namespace esphome {
namespace http_request {

struct Header {
  std::string name;
  std::string value;
};

struct HttpContainer {
  int status_code{-1};
  void end() {}
};

class HttpRequestComponent {
 public:
  std::shared_ptr<HttpContainer> start(const std::string & /*url*/,
                                       const std::string & /*method*/,
                                       const std::string & /*body*/,
                                       const std::vector<Header> & /*headers*/,
                                       const std::vector<std::string> & /*collect*/) {
    return nullptr;
  }
};

}  // namespace http_request
}  // namespace esphome
