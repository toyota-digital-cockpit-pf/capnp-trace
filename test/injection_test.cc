#include "injection.h"

#include <gtest/gtest.h>

class InjectionTest : public ::testing::Test {};

TEST_F(InjectionTest, CreateInstance) {
  // Arrange

  // Act
  capnp_trace::Injection injection{"method@signal=KILL@when=3"};

  // Assert
  ASSERT_EQ("method", injection.method_);
  ASSERT_EQ(SIGKILL, injection.signal_);
  ASSERT_EQ(3, injection.when_);
}

TEST_F(InjectionTest, DontCallbackWhenIsNotSatisfied) {
  // Arrange
  capnp_trace::Injection injection{"method@signal=KILL@when=3"};
  bool is_called = false;
  auto callback = [&is_called](...) { is_called = true; };

  // Act
  injection.Check("method", callback);
  injection.Check("method", callback);
  injection.Check("method2", callback);

  // Assert
  ASSERT_FALSE(is_called);
}

TEST_F(InjectionTest, CallbackWhenIsSatisfied) {
  // Arrange
  capnp_trace::Injection injection{"method@signal=KILL@when=3"};
  bool is_called = false;
  int signal = 0;
  auto callback = [&is_called, &signal](int sig) {
    is_called = true;
    signal = sig;
  };

  // Act
  injection.Check("method", callback);
  injection.Check("method", callback);
  injection.Check("method", callback);

  // Assert
  ASSERT_TRUE(is_called);
  ASSERT_EQ(SIGKILL, signal);
}

TEST_F(InjectionTest, CallbackWithSigAbrtWhenIsSatisfied) {
  // Arrange
  capnp_trace::Injection injection{"method@signal=ABRT@when=1"};
  bool is_called = false;
  int signal = 0;
  auto callback = [&is_called, &signal](int sig) {
    is_called = true;
    signal = sig;
  };

  // Act
  injection.Check("method", callback);

  // Assert
  ASSERT_TRUE(is_called);
  ASSERT_EQ(SIGABRT, signal);
}
