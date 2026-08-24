#define BOOST_TEST_MODULE BcosOpstackBlockTests
#include "support/ReplayGate.h"
#include <boost/test/unit_test.hpp>

// Route every op_replay failure through Boost.Test. ReplayGate.h is included by
// both boost and non-boost TUs in this binary; the routing is a runtime hook
// (ReplayReport::sink) rather than an #ifdef'd inline body — two TUs giving the
// same inline function different bodies is an ODR violation, and the linker may
// silently pick the non-boost definition, dropping the BOOST_ERROR routing.
namespace
{
struct InstallReplaySink
{
    InstallReplaySink()
    {
        ::op_replay::replayReport().sink = [](const std::string& msg) { BOOST_ERROR(msg); };
    }
};
InstallReplaySink g_installReplaySink;
}  // namespace
