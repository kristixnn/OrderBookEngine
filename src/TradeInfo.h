#pragma once
#include "Usings.h"
#include "Order.h"

struct TradeInfo {
	OrderId orderId_;
	Price price_;
	Quantity quantity_;
};
