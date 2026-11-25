#pragma once

#include <QString>

#include <string>

// APP_VERSION is set by CMake from project(VERSION ...); the fallback keeps IDE builds working.
#ifndef APP_VERSION
#define APP_VERSION "1.0.1"
#endif

#define APP_NAME "Paint Matcher"
#define APP_ORGANIZATION "PaintMatcher"

// The core library uses UTF-8 std::string; Qt 6 converts std::string as UTF-8.
inline QString toQ(const std::string& s) { return QString::fromStdString(s); }
inline std::string toStd(const QString& s) { return s.toStdString(); }

// Compiler used to build the application, e.g. "MSVC 19.38 (1938...)" or "GCC 13.2.0".
QString compilerDescription();
