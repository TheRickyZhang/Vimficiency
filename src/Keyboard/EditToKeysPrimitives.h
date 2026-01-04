#pragma once

#include "EditToKeys.h"

#include <functional>
#include <initializer_list>

// Use reference_wrapper to avoid overhead of copying, while giving concrete objects to initializer list
EditToKeys combineAll(std::initializer_list<std::reference_wrapper<const EditToKeys>> maps);
