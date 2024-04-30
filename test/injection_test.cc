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
  auto callback  = [&is_called]() { is_called = true; };

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
  auto callback  = [&is_called]() { is_called = true; };

  // Act
  injection.Check("method", callback);
  injection.Check("method", callback);
  injection.Check("method", callback);

  // Assert
  ASSERT_TRUE(is_called);
}
