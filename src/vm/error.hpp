#pragma once

#include "base/error.hpp"


namespace vm
{

class VmError : public base::Error
{
    using Error::Error;
};


class RevertError : public base::Error
{
    using Error::Error;
};

} // namespace vm