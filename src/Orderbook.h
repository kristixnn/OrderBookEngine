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
	Orderbook& operator=(const Orderbook&) = delete;
	Orderbook(const Orderbook&&) = delete;
	Orderbook& operator=(Orderbook&&) = delete;

	Trades AddOrder(OrderType type, OrderId id, Side side, Price price, Quantity quantity);
	void CancelOrder(OrderId orderId);
	//Trades MatchOrder(OrderModify order);
	Trades ModifyOrder(OrderModify order);

	std::size_t Size() const;
	OrderbookLevelInfos GetOrderInfos() const;
	//OrderPointer AllocateOrder(OrderType type, OrderId id, Side side, Price price, Quantity quantity);
private:
	struct OrderEntry {
		OrderPointer order_{ nullptr };
		//OrderPointers::iterator location_;

	};

	struct LevelData {
		Quantity quantity_{0};
		//Quantity count_{};
		IntrusiveOrderList orders;
		Quantity count_{ 0 };
		enum class Action {
			Add,
			Remove,
			Match
		};
	};
	Price bestBidPrice_{ -1 };							  // Tracks the highest bid price
	Price bestAskPrice_{ static_cast<Price>(MAX_PRICE) }; // Tracks the lowest ask price

	//switching from map to a flat array indexed by a price tick
	//std::unordered_map<Price, LevelData> data_;
	std::vector<LevelData> data_;

	//same change for bids and asks, the sorting will be done manually
	//storing the bids ascending order from highest ask
	//std::map<Price, IntrusiveOrderList, std::greater<Price>> bids_;
	std::vector<LevelData> bids_;

	//storing the asks descending order from lowest bid
	//std::map<Price, IntrusiveOrderList, std::less<Price>> asks_;
	std::vector< LevelData> asks_;

	//storing the orders
	static constexpr std::size_t MAX_PRICE = 100000;
	static constexpr std::size_t MAX_ORDERS = 300000;
	//std::unordered_map<OrderId, OrderEntry> orders_;
	std::vector<OrderEntry> orders_;
 
	OrderPool<300000> orderPool_;
	mutable std::mutex ordersMutex_;
	
	std::condition_variable shutdownConditionVariable_;
	std::atomic<bool> shutdown_{ false };
	std::thread ordersPruneThread_;

	void PruneGoodForDayOrders();
	Trades AddOrderInternal(OrderType type, OrderId id, Side side, Price price, Quantity quantity);
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