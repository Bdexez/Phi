#pragma once

// Contract shared by the test plugin and the tests. Like any engine
// interface, it is the only thing both sides have in common.

namespace phi::tests {

class ITestService {
public:
    virtual ~ITestService() = default;
    [[nodiscard]] virtual int answer() const = 0;
    [[nodiscard]] virtual int pingsReceived() const = 0;
    [[nodiscard]] virtual int updatesRun() const = 0;
};

struct TestPing {
    int value = 0;
};

struct TestPong {
    int value = 0;
};

}  // namespace phi::tests
