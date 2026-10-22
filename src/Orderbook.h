#pragma once
#include "Usings.h"
#include "Trade.h"
#include "Order.h"
#include "OrderType.h"
#include "OrderModify.h"
#include "OrderbookLevelInfos.h"
#include "IntrusiveOrderList.h"
#include "OrderPool.h"


class Orderbook {
public:
	Orderbook();
	~Orderbook();
	Orderbook(const Orderbook&) = delete;
	void operator=(const Orderbook&) = delete;
	Orderbook(const Orderbook&&) = delete;
	void operator=(const Orderbook&&) = delete;

	Trades AddOrder(OrderPointer order);
	void CancelOrder(OrderId orderId);
	//Trades MatchOrder(OrderModify order);
	Trades ModifyOrder(OrderModify order);

	std::size_t Size() const;
	OrderbookLevelInfos GetOrderInfos() const;
	OrderPointer AllocateOrder(OrderType type, OrderId id, Side side, Price price, Quantity quantity);
private:
	struct OrderEntry {
		OrderPointer order_{ nullptr };
		//OrderPointers::iterator location_;

	};

	struct LevelData {
		Quantity quantity_{};
		Quantity count_{};
		enum class Action {
			Add,
			Remove,
			Match
		};
	};

	std::unordered_map<Price, LevelData> data_;
	std::map<Price, IntrusiveOrderList, std::greater<Price>> bids_;//storing the bids ascending order from highest ask
	//std::vector<Price, std::greater<Price>> bids_;
	std::map<Price, IntrusiveOrderList, std::less<Price>> asks_;//storing the asks descending order from lowest bid
	//std::vector<Price, std::less<Price>> asks_;
	std::unordered_map<OrderId, OrderEntry> orders_;//storing the orders
	OrderPool<100000> orderPool_;
	mutable std::mutex ordersMutex_;
	std::thread ordersPruneThread_;
	std::condition_variable shutdownConditionVariable_;
	std::atomic<bool> shutdown_{ false };
	
	void PruneGoodForDayOrders();

	void CancelOrders(OrderIds orderIds);
	void CancelOrderInternal(OrderId orderId);

	void OnOrderCancelled(OrderPointer order);
	void OnOrderAdded(OrderPointer order);
	void OnOrderMatched(Price price,Quantity quantity, bool isFullyFilled);
	void UpdateLevelData(Price price, Quantity quantity, LevelData::Action action);

	bool CanMatch(Side side, Price price) const;
	bool CanFullyFill(Side side, Price price, Quantity quantity) const;

	Trades MatchTrades();
};