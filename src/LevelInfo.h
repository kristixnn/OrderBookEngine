#pragma once
#include"Usings.h"

//std::int32_t Price; for orderbook is a no, this is variable declaration
using Price = std::int32_t;//type alias for price as an interger, basically when Price is called it calls int32_t
using Quantity = std::uint32_t;
using OrderId = std::uint64_t;
struct LevelInfo {
	Price price_;
	Quantity quantity;
};
//same applies for vectors
using LevelInfos = std::vector<LevelInfo>;