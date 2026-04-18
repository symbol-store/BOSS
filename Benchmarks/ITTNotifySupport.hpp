#ifdef WITH_ITT_NOTIFY
#include <ittnotify.h>
#include <sstream>
#endif // WITH_ITT_NOTIFY

class VTuneAPIInterface {
#ifdef WITH_ITT_NOTIFY
  ___itt_domain* domain;
#endif // WITH_ITT_NOTIFY

public:
#ifdef WITH_ITT_NOTIFY
  explicit VTuneAPIInterface(char const* name) : domain(__itt_domain_create(name)) {};
#else
  explicit VTuneAPIInterface(char const*) {};
#endif // WITH_ITT_NOTIFY
  template <typename... DescriptorTypes>
  void startSampling(DescriptorTypes... tasknameComponents) const {
#ifdef WITH_ITT_NOTIFY
    std::stringstream taskname;
    (taskname << ... << tasknameComponents);
    auto task = __itt_string_handle_create(taskname.str().c_str());
    __itt_resume();
    __itt_task_begin(domain, __itt_null, __itt_null, task);
#endif // WITH_ITT_NOTIFY
  }
  void stopSampling() const {
#ifdef WITH_ITT_NOTIFY
    __itt_task_end(domain);
    __itt_pause();
#endif // WITH_ITT_NOTIFY
  }
};
