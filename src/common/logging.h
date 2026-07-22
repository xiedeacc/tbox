/*******************************************************************************
 * Copyright (c) 2024  xiedeacc.com.
 * All rights reserved.
 *******************************************************************************/

#ifndef TBOX_COMMON_LOGGING_H_
#define TBOX_COMMON_LOGGING_H_

#include <cstdlib>
#include <memory>
#include <ostream>
#include <sstream>
#include <string>

namespace tbox {
namespace logging {

enum class Severity {
  INFO,
  WARNING,
  ERROR,
  FATAL,
};

void Initialize(const std::string& program_name,
                const std::string& log_dir = "./logs");
void Shutdown();
std::string CommandLine(int argc, char** argv);

class LogMessage {
 public:
  LogMessage(const char* file, int line, Severity severity);
  LogMessage(const LogMessage&) = delete;
  LogMessage& operator=(const LogMessage&) = delete;
  ~LogMessage();

  std::ostream& stream() { return stream_; }

 private:
  const char* file_;
  int line_;
  Severity severity_;
  std::ostringstream stream_;
};

class FatalLogMessage {
 public:
  FatalLogMessage(const char* file, int line);
  FatalLogMessage(const FatalLogMessage&) = delete;
  FatalLogMessage& operator=(const FatalLogMessage&) = delete;
  [[noreturn]] ~FatalLogMessage();

  std::ostream& stream() { return stream_; }

 private:
  const char* file_;
  int line_;
  std::ostringstream stream_;
};

class CheckMessage {
 public:
  CheckMessage(bool condition, const char* file, int line, const char* expr);
  CheckMessage(const CheckMessage&) = delete;
  CheckMessage& operator=(const CheckMessage&) = delete;
  ~CheckMessage();

  std::ostream& stream() { return stream_; }

 private:
  bool failed_;
  const char* file_;
  int line_;
  std::ostringstream stream_;
};

}  // namespace logging
}  // namespace tbox

#ifdef LOG
#undef LOG
#endif
#ifdef CHECK
#undef CHECK
#endif
#ifdef DCHECK
#undef DCHECK
#endif
#ifdef CHECK_OP
#undef CHECK_OP
#endif
#ifdef CHECK_EQ
#undef CHECK_EQ
#endif
#ifdef CHECK_NE
#undef CHECK_NE
#endif
#ifdef CHECK_LT
#undef CHECK_LT
#endif
#ifdef CHECK_LE
#undef CHECK_LE
#endif
#ifdef CHECK_GT
#undef CHECK_GT
#endif
#ifdef CHECK_GE
#undef CHECK_GE
#endif

#define LOG(level) TBOX_LOG_##level(__FILE__, __LINE__)

#define TBOX_LOG_INFO(file, line) \
  ::tbox::logging::LogMessage(file, line, \
                              ::tbox::logging::Severity::INFO) \
      .stream()
#define TBOX_LOG_WARNING(file, line) \
  ::tbox::logging::LogMessage(file, line, \
                              ::tbox::logging::Severity::WARNING) \
      .stream()
#define TBOX_LOG_ERROR(file, line) \
  ::tbox::logging::LogMessage(file, line, \
                              ::tbox::logging::Severity::ERROR) \
      .stream()
#define TBOX_LOG_FATAL(file, line) \
  ::tbox::logging::FatalLogMessage(file, line).stream()

#define CHECK(condition) \
  ::tbox::logging::CheckMessage(static_cast<bool>(condition), __FILE__, \
                                __LINE__, "Check failed: " #condition " ") \
      .stream()

#define DCHECK(condition) CHECK(condition)

#define CHECK_OP(op_name, op, value1, value2) \
  ::tbox::logging::CheckMessage(((value1) op (value2)), __FILE__, __LINE__, \
                                "Check failed: " #value1 " " #op_name " " \
                                #value2 " ") \
      .stream()

#define CHECK_EQ(value1, value2) CHECK_OP(==, ==, value1, value2)
#define CHECK_NE(value1, value2) CHECK_OP(!=, !=, value1, value2)
#define CHECK_LT(value1, value2) CHECK_OP(<, <, value1, value2)
#define CHECK_LE(value1, value2) CHECK_OP(<=, <=, value1, value2)
#define CHECK_GT(value1, value2) CHECK_OP(>, >, value1, value2)
#define CHECK_GE(value1, value2) CHECK_OP(>=, >=, value1, value2)

#endif  // TBOX_COMMON_LOGGING_H_
