#include"Orderbook.h"

int main() {
	Orderbook orderbook;

	/*const OrderId orderId = 1;
	
	orderbook.AddOrder(std::make_shared<Order>(OrderType::GoodTillCancel, orderId, Side::Buy, 100, 10));
	std::cout << orderbook.Size() << std::endl;

	orderbook.CancelOrder(orderId);
	std::cout << orderbook.Size() << std::endl;
	*/

	OrderPointer buy = orderbook.AllocateOrder(OrderType::GoodTillCancel, 1, Side::Buy, 100, 10);
	if (buy) {
		Trades trades = orderbook.AddOrder(buy);
		std::cout << "Buy Trades executed: " << trades.size() << "\n";
	}
	OrderPointer sell = orderbook.AllocateOrder(OrderType::GoodTillCancel, 2, Side::Sell, 100, 5);
	if (sell) {
		Trades trades2 = orderbook.AddOrder(sell);
		std::cout << "Sell Trades executed: " << trades2.size() << "\n";
	}
	orderbook.CancelOrder(1);
	return 0;
}