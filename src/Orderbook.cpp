#include "Orderbook.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iterator>
#include <mutex>
#include <numeric>
#include <cassert>
#include <ranges>

Trades Orderbook::AddOrder(OrderType type, OrderId id, Side side, Price price, Quantity quantity)
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	return AddOrderInternal(type, id, side, price, quantity);
}

Trades Orderbook::AddOrderInternal(OrderType type, OrderId id, Side side, Price price, Quantity quantity)
{
	//auto& orderId = orders_[id];
	if (id >= orders_.size()) {
		orders_.resize(id + 10000);
	}
	if (orders_[id].order_!=nullptr)
		return {};
	
	if (type == OrderType::FillAndKill && !CanMatch(side, price))
		return {};

	if (type == OrderType::FillOrKill && !CanFullyFill(side, price, quantity))
		return {};

	// Ensure price vectors are large enough for this price tick
	if (price >= static_cast<Price>(bids_.size())) {
		Price newSize = price + 1000;
		bids_.resize(newSize);
		asks_.resize(newSize);
		data_.resize(newSize);
	}

	if (type == OrderType::Market)
	{
		if (side == Side::Buy && !asks_.empty())
		{
			auto firstActiveAsk = std::ranges::find_if(asks_, [](const LevelData& level) {
				return level.count_ > 0;
			});
			if(firstActiveAsk != asks_.end()){
				price = std::distance(asks_.begin(), firstActiveAsk);
				type = OrderType::GoodTillCancel;
			}
		}
		else if (side == Side::Sell && !bids_.empty())
		{
			auto firstActiveBid = std::ranges::find_if(bids_, [](const LevelData& level) {
				return level.count_ > 0;
				});
			if (firstActiveBid != bids_.end()) {
				price = std::distance(bids_.begin(), firstActiveBid);
				type = OrderType::GoodTillCancel;
			}
		}
		else
			return {};
	}

	Order* order = orderPool_.allocate();
	if (order == nullptr)
		return {};

	order->init(type, id, side, price, quantity);

	if (side == Side::Buy)
	{
		auto& orders = bids_[price];
		bids_[price].orders.PushBack(order);
		bids_[price].count_++;
		bids_[price].quantity_ += quantity;
		if (price > bestBidPrice_)
			bestBidPrice_ = price;
	}
	else
	{
		auto& orders = asks_[price];
		asks_[price].orders.PushBack(order);
		asks_[price].count_++;
		asks_[price].quantity_ += quantity;
		if (price < bestAskPrice_)
			bestAskPrice_ = price;
	}

	orders_[id].order_=order;
	OnOrderAdded(order);
	return MatchTrades();
}

void Orderbook::CancelOrder(OrderId orderId)
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	CancelOrderInternal(orderId);
}

void Orderbook::CancelOrders( OrderIds orderIds)
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	for (const auto& orderId : orderIds)
		CancelOrderInternal(orderId);
}

void Orderbook::CancelOrderInternal(OrderId orderId)
{
	if (orderId >= orders_.size() || orders_[orderId].order_ == nullptr)
		return;

	//auto order = orders_.at(orderId).order_;
	//orders_.erase(orderId);
	OrderPointer order = orders_[orderId].order_;
	orders_[orderId].order_=nullptr;

	const Price price = order->GetPrice();
	const Quantity quantity = order->GetRemainingQuantity();
	if (order->GetSide() == Side::Sell) {
		//after switching to vector, every metric needs to be manually updated

		//auto& orders = asks_[price];
		/*if (asks_[price].orders.Empty()) {
			asks_.erase(price);
		}
		*/

		asks_[price].orders.Remove(order);
		asks_[price].count_--;
		asks_[price].quantity_-=quantity;
	}
	else {
		//auto& orders = bids_[price];
		/*if (bids_[price].orders.Empty()) {
			bids_.erase(price);
		}
		*/

		bids_[price].orders.Remove(order);
		bids_[price].count_--;
		bids_[price].quantity_ -= quantity;
	}
	OnOrderCancelled(order);
	order->reset();
	orderPool_.deallocate(order);
}

Trades Orderbook::ModifyOrder(OrderModify order)
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	//const auto it = orders_.find(order.GetOrderId());
	auto id = order.GetOrderId();

	if (id >= orders_.size()||orders_[id].order_ == nullptr)
		return {};

	OrderType orderType = orders_[id].order_->GetOrderType();
	CancelOrderInternal(id);
	return AddOrderInternal(orderType, id, order.GetSide(), order.GetPrice(), order.GetQuantity());
}

std::size_t Orderbook::Size() const
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	return std::count_if(orders_.begin(), orders_.end(), [](const auto& entry) {
		return entry.order_ != nullptr;
	});
}

OrderbookLevelInfos Orderbook::GetOrderInfos() const
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	LevelInfos bidInfos, askInfos;
	bidInfos.reserve(orders_.size());
	askInfos.reserve(orders_.size());

	auto CreateLevelInfos = [](Price price, const IntrusiveOrderList& orders) {
		Quantity total = 0;
		const Order* current = orders.Front();
		while (current != nullptr) {
			total += current->GetRemainingQuantity();
			current = current->next;
		}
		return LevelInfo{ price, total };
	};

	//this now becomes an indexed loop and requires manual sorting, because the map to vector switch
	/*
	for (const auto& [price, orders] : bids_)
	{
		bidInfos.push_back(CreateLevelInfos(price, orders));
	}
	for (const auto& [price, orders] : asks_)
	{
		askInfos.push_back(CreateLevelInfos(price, orders));
	}
	*/
	
	//descending iteration for bids
	for (Price price = static_cast<Price>(bids_.size()) - 1; price >= 0; --price) {
		const auto& level = bids_[price];
		if (level.count_ > 0)
			bidInfos.push_back(LevelInfo{ price,level.quantity_ });
	}

	//ascending iteration for asks
	for (Price price = 0; price < static_cast<Price>(asks_.size()); ++price) {
		const auto& level = asks_[price];
		if (level.count_ > 0)
			askInfos.push_back(LevelInfo{ price,level.quantity_ });
	}
	return OrderbookLevelInfos{ bidInfos, askInfos };
}

bool Orderbook::CanMatch(Side side, Price price) const
{
	if (side == Side::Buy)
	{
		if (asks_.empty())
		{
			return false;
		}
		//const auto& [bestAsk, _] = *asks_.begin();
		Price bestAsk = -1;
		for (Price price = 0; price < static_cast<Price>(asks_.size()); ++price) {
			if (asks_[price].count_ > 0)
				bestAsk = price;
		}
		return price >= bestAsk;
	}
	else
	{
		if (bids_.empty())
		{
			return false;
		}
		//const auto& [bestBid, _] = *bids_.begin();
		Price bestBid=-1;
		for (Price price = static_cast<Price>(bids_.size()) - 1; price >= 0; --price) {
			if (bids_[price].count_>0)
				bestBid=price;
		}
		return price <= bestBid;
	}
}

Trades Orderbook::MatchTrades()
{
	Trades trades;
	trades.reserve(orders_.size());

	while (true)
	{
		//this caused vector subscript out of range error
		/*
		if (bids_.empty() || asks_.empty())
		{
			break;
		}
		*/
		//auto& [bidPrice, bids] = *bids_.begin();
		//auto& [askPrice, asks] = *asks_.begin();
		Price bidPrice = -1;
		for (Price price = static_cast<Price>(bids_.size()) - 1; price >= 0; --price) {
			if (bids_[price].count_ > 0){
				bidPrice = price;
				break;
			}
		}
		
		Price askPrice = -1;
		for (Price price = 0; price < static_cast<Price>(asks_.size()); ++price) {
			if (asks_[price].count_ > 0){
				askPrice = price;
				break;
			}
		}

		if (bidPrice == -1 || askPrice == -1)
		{
			break;
		}
		if (bidPrice < askPrice) {
			break;
		}

		while (!bids_[bidPrice].orders.Empty()&& !asks_[askPrice].orders.Empty())
		{
			auto bid = bids_[bidPrice].orders.Front();
			auto ask = asks_[askPrice].orders.Front();

			Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());
			bid->Fill(quantity);
			ask->Fill(quantity);

			bids_[bidPrice].quantity_ -= quantity;
			asks_[askPrice].quantity_ -= quantity;
			
			// Capture local copies before reset() / deallocate()
			OrderId bidId = bid->GetOrderId();
			Price bidPriceVal = bid->GetPrice();
			bool bidFilled = bid->IsFilled();

			OrderId askId = ask->GetOrderId();
			Price askPriceVal = ask->GetPrice();
			bool askFilled = ask->IsFilled();
			
			if (bidFilled)
			{
				bids_[bidPrice].orders.PopFront();
				bids_[bidPrice].count_=0;
				//OrderPointer bidOrder = orders_[bidId].order_;
				orders_[bidId].order_ = nullptr;
			}
			if (askFilled)
			{
				asks_[askPrice].orders.PopFront();
				asks_[askPrice].count_=0;
				//orders_.erase(askId);
				//OrderPointer order = orders_[askId].order_;
				orders_[askId].order_ = nullptr;
			}

			trades.push_back(Trade{
				TradeInfo{bidId, bidPriceVal, quantity},
				TradeInfo{askId, askPriceVal, quantity}
				});

			OnOrderMatched(bidPriceVal, quantity, bidFilled);
			OnOrderMatched(askPriceVal, quantity, askFilled);

			if (bidFilled)
			{
				bid->reset();
				orderPool_.deallocate(bid);
			}
			if (askFilled)
			{
				ask->reset();
				orderPool_.deallocate(ask);
			}
		}
		
		if (bids_[bidPrice].orders.Empty())
		{
			//bids_.erase(bidPrice);
			//bids_[bidPrice].orders.Empty();
			bids_[bidPrice].count_=0;
			bids_[bidPrice].quantity_ =0;
		}
		if (asks_[askPrice].orders.Empty())
		{
			//asks_.erase(askPrice);
			//asks_[bidPrice].orders.Empty();
			asks_[askPrice].count_=0;
			asks_[askPrice].quantity_ = 0;
		}
	}

	
	//auto& [_, bidsList] = *bids_.begin();
	Price bestBidPrice = -1;
	for (Price price = static_cast<Price>(bids_.size()) - 1; price >= 0; --price) {
		if (bids_[price].count_ > 0) {
			bestBidPrice = price;
			break;
		}
	}
	if (bestBidPrice != -1 && !bids_[bestBidPrice].orders.Empty()) {
		auto order = bids_[bestBidPrice].orders.Front();
		if (order->GetOrderType() == OrderType::FillAndKill)
			CancelOrderInternal(order->GetOrderId());
	}
	
	//auto& [_, asksList] = *asks_.begin();
	Price bestAskPrice = -1;
	for (Price price = 0; price < static_cast<Price>(asks_.size()); ++price) {
		if (asks_[price].count_ > 0) {
			bestAskPrice = price;
			break;
		}
	}
	if (bestAskPrice != -1 && !asks_[bestAskPrice].orders.Empty()) {
		auto order = asks_[bestAskPrice].orders.Front();
		if (order->GetOrderType() == OrderType::FillAndKill)
			CancelOrderInternal(order->GetOrderId());
	}
		
	
	return trades;
}

void Orderbook::PruneGoodForDayOrders() {
	using namespace std::chrono;
	const auto end = hours(16);

	while (true)
	{
		const auto now = system_clock::now();
		const auto now_c = system_clock::to_time_t(now);
		std::tm now_parts{};
		#ifdef _WIN32
		localtime_s(&now_parts, &now_c);
		#else
		localtime_r(&now_c, &now_parts);
		#endif
		if (now_parts.tm_hour >= end.count())
		{
			now_parts.tm_mday += 1;
		}
		now_parts.tm_hour = end.count();
		now_parts.tm_min = 0;
		now_parts.tm_sec = 0;
		now_parts.tm_isdst = -1;

		auto next = system_clock::from_time_t(std::mktime(&now_parts));
		auto till = next - now + milliseconds(100);

		{
			std::unique_lock ordersLock{ ordersMutex_ };
			if (shutdown_.load(std::memory_order_acquire) ||
				shutdownConditionVariable_.wait_for(ordersLock, till, [this] { return shutdown_.load(std::memory_order_acquire); }))
				return;
		}

		OrderIds orderIds;
		{
			std::scoped_lock ordersLock{ ordersMutex_ };
			for (const auto& entry : orders_)
			{
				if (entry.order_ == nullptr)
					continue;
				auto order = entry.order_;
				if (order->GetOrderType() != OrderType::GoodForDay)
					continue;
				orderIds.push_back(order->GetOrderId());
			}
		}

		CancelOrders(orderIds);
	}
}

Orderbook::~Orderbook() {
	{
		std::scoped_lock ordersLock{ ordersMutex_ };
		shutdown_.store(true, std::memory_order_release);
	}
	shutdownConditionVariable_.notify_all();
	ordersPruneThread_.join();
}

Orderbook::Orderbook() : ordersPruneThread_{ [this] { PruneGoodForDayOrders(); } },
bids_(MAX_PRICE),asks_(MAX_PRICE),data_(MAX_PRICE),orders_(MAX_ORDERS)
{}

void Orderbook::OnOrderCancelled(OrderPointer order)
{
	UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Remove);
}

void Orderbook::OnOrderAdded(OrderPointer order)
{
	UpdateLevelData(order->GetPrice(), order->GetRemainingQuantity(), LevelData::Action::Add);
}

void Orderbook::OnOrderMatched(Price price, Quantity quantity, bool isFullyFilled)
{
	UpdateLevelData(price, quantity, isFullyFilled ? LevelData::Action::Remove : LevelData::Action::Match);
}

void Orderbook::UpdateLevelData(Price price, Quantity quantity, LevelData::Action action)
{
	auto& data = data_[price];
	data.count_ += action == LevelData::Action::Remove ? -1 : action == LevelData::Action::Add ? 1 : 0;

	if (action == LevelData::Action::Remove || action == LevelData::Action::Match)
	{
		data.quantity_ -= quantity;
	}
	else
	{
		data.quantity_ += quantity;
	}
	if (data.count_ == 0){
		//data_[price].orders.Empty();
		data_[price].count_ = 0;
		data_[price].quantity_=0;
	}
}

bool Orderbook::CanFullyFill(Side side, Price price, Quantity quantity) const
{
	if (!CanMatch(side, price))
		return false;

	//std::optional<Price> threshold;
	Quantity accumulatedQuantity = 0;
	if (side == Side::Buy)
	{
		//const auto [askPrice, _] = *asks_.begin();
		Price askPrice = -1;
		for (Price price = 0; price < static_cast<Price>(asks_.size()); ++price) {
			if (asks_[price].count_ > 0) {
				askPrice = price;
				break;
			}
		}
		//threshold = askPrice;

		for (Price priceCmp = askPrice; priceCmp <= price && priceCmp < static_cast<Price>(asks_.size()); ++priceCmp) {
			if (asks_[priceCmp].count_ > 0)
			{
				accumulatedQuantity += asks_[priceCmp].quantity_;
				if (accumulatedQuantity >= quantity)
					return true;
			}
		}
	}
	else
	{
		//const auto [bidPrice, _] = *bids_.begin();
		Price bidPrice = -1;
		for (Price price = static_cast<Price>(bids_.size()) - 1; price >= 0; --price) {
			if (bids_[price].count_ > 0) {
				bidPrice = price;
				break;
			}
		}
		//threshold = bidPrice;
		for (Price priceCmp = bidPrice; priceCmp >= price && priceCmp >= 0; --priceCmp) {
			if (bids_[priceCmp].count_ > 0)
			{
				accumulatedQuantity += bids_[priceCmp].quantity_;
				if (accumulatedQuantity >= quantity)
					return true;
			}
		}
	}

	/*for (const auto& levelData : data_)
	{
		if (threshold.has_value() && ((side == Side::Buy && threshold.value() > levelData) || (side == Side::Sell && threshold.value() < levelPrice)))
			continue;
		if ((side == Side::Buy && levelPrice > price) || (side == Side::Sell && levelPrice < price))
			continue;
		if (quantity <= levelData.quantity_)
			return true;
		quantity -= levelData.quantity_;
	}
	*/
	return false;
}