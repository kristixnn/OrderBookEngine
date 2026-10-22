#include "Orderbook.h"

Trades Orderbook::AddOrder(OrderPointer order)
{
	std::scoped_lock ordersLock{ ordersMutex_ };

	auto poolCleanup = [this](Order* order) {
		order->reset();
		orderPool_.deallocate(order);
	};

	std::unique_ptr<Order, decltype(poolCleanup)> guard(order, poolCleanup);

	if (orders_.contains(order->GetOrderId()))
		return{  };

	if (order->GetOrderType() == OrderType::FillAndKill && !CanMatch(order->GetSide(), order->GetPrice()))
		return{};//if its fillandkill and cannot match by side and price - dont do anything, if it is fillandkill ->fills,matches,executes

	if (order->GetOrderType() == OrderType::FillOrKill && !CanFullyFill(order->GetSide(), order->GetPrice(),order->GetInitialQuantity()))
		return{};//if its fillorkill and cannot be fully filled - dont do anything, if it is fillorkill -> fills,matches,executes

	if (order->GetOrderType() == OrderType::Market)
	{
		if (order->GetSide() == Side::Buy && !asks_.empty())
		{
			
			const auto& [worstAsk, _] = *asks_.rbegin();
			order->ToGoodTillCancel(worstAsk);
		}
		else if (order->GetSide() == Side::Sell && !bids_.empty())
		{
			
			const auto& [worstBid, _] = *bids_.rbegin();
			order->ToGoodTillCancel(worstBid);
		}
		else
			return{};
	}

	//OrderPointers::iterator iterator;
	if (order->GetSide() == Side::Buy)
	{
		
		auto& orders = bids_[order->GetPrice()];
		orders.PushBack(order);
		//iterator = std::prev(orders.end());
	}
	else
	{
		
		auto& orders = asks_[order->GetPrice()];
		orders.PushBack(order);
		//iterator = std::prev(orders.end());
	}
	orders_.insert({ order->GetOrderId(),OrderEntry{order} });

	guard.release();
	OnOrderAdded(order);
	return MatchTrades();
}
void Orderbook::CancelOrder(OrderId orderId)
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	if (!orders_.contains(orderId))
		return;
	auto order = orders_.at(orderId).order_;
	

	if (order->GetSide() == Side::Sell) {
		auto price = order->GetPrice();
		auto& orders = asks_.at(price);
		orders.Remove(order);
		//orders.(orderIterator);
		if (orders.Empty()){
			asks_.erase(price);
			data_.erase(price);
		}
	}
	else
	{
		auto price = order->GetPrice();
		auto& orders = bids_.at(price);
		orders.Remove(order);
		//orders.erase(orderIterator);
		if (orders.Empty())
		{
			bids_.erase(price);
			data_.erase(price);
		}
	}
	orders_.erase(orderId);
	order->reset();
	orderPool_.deallocate(order);
}

void Orderbook::CancelOrders(OrderIds orderIds)
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	for (const auto& orderId : orderIds)
		CancelOrderInternal(orderId);
}

void Orderbook::CancelOrderInternal(OrderId orderId)
{
	if (!orders_.contains(orderId))
		return;

	 auto order = orders_.at(orderId).order_;
	orders_.erase(orderId);

	if (order->GetSide() == Side::Sell) {
		auto price = order->GetPrice();
		auto& orders = asks_.at(price);
		orders.Remove(order);
		//orders.erase(orderIterator);
		if (orders.Empty()){
			asks_.erase(price);
			data_.erase(price);
		}
	}
	else
	{
		auto price = order->GetPrice();
		auto& orders = bids_.at(price);
		orders.Remove(order);
		//orders.erase(orderIterator);
		if (orders.Empty()){
			bids_.erase(price);
			data_.erase(price);
		}
	}
	OnOrderCancelled(order);
	order->reset();
	orderPool_.deallocate(order);
}

/*Trades Orderbook::MatchOrder(OrderModify order)
{
	if (!orders_.contains(order.GetOrderId()))
		return{};

	const auto& [existingOrder, _] = orders_.at(order.GetOrderId());
	CancelOrder(order.GetOrderId());
	return AddOrder(order.ToOrderPointer(existingOrder->GetOrderType()));
}*/
Trades Orderbook::ModifyOrder(OrderModify order) {
	OrderType orderType;

	{
		std::scoped_lock ordersLock{ ordersMutex_ };
		if (!orders_.contains(order.GetOrderId()))
			return{};

		auto existingOrder = orders_.at(order.GetOrderId()).order_;
		orderType = existingOrder->GetOrderType();
		
		
	}
	CancelOrder(order.GetOrderId());
	Order* newOrder = orderPool_.allocate();
	if (newOrder == nullptr)
		return{};
	newOrder->init(orderType, order.GetOrderId(), order.GetSide(), order.GetPrice(), order.GetQuantity());
	return AddOrder(newOrder);
}

std::size_t Orderbook::Size() const
{
	std::scoped_lock ordersLock{ ordersMutex_ };
	return orders_.size();
}

OrderbookLevelInfos Orderbook::GetOrderInfos() const 
{
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
		return LevelInfo{ price,total };
		};
	for (const auto& [price, orders] : bids_)
	{
		bidInfos.push_back(CreateLevelInfos(price, orders));
	}
	for (const auto& [price, orders] : asks_)
	{
		askInfos.push_back(CreateLevelInfos(price, orders));
	}
	return OrderbookLevelInfos{ bidInfos,askInfos };
}

bool Orderbook::CanMatch(Side side, Price price) const
{
	if (side == Side::Buy)
	{
		if (asks_.empty())
		{
			return false;
		}
		const auto& [bestAsk, _] = *asks_.begin();
		return price >= bestAsk;
	}
	else
	{
		if (bids_.empty())
		{
			return false;
		}
		const auto& [bestBid, _] = *bids_.begin();
		return price <= bestBid;
	}
}

Trades Orderbook::MatchTrades()
{
	Trades trades;
	trades.reserve(orders_.size());

	while (true)
	{
		if (bids_.empty() || asks_.empty())
		{
			break;
		}

		auto& [bidPrice, bids] = *bids_.begin();
		auto& [askPrice, asks] = *asks_.begin();

		if (bidPrice < askPrice) {
			break;
		}
		while (!bids.Empty() && !asks.Empty())
		{
			auto bid = bids.Front();
			auto ask = asks.Front();

			Quantity quantity = std::min(bid->GetRemainingQuantity(), ask->GetRemainingQuantity());
			bid->Fill(quantity);
			ask->Fill(quantity);

			if (bid->IsFilled()) 
			{
				bids.PopFront();
				orders_.erase(bid->GetOrderId());
				bid->reset();
				orderPool_.deallocate(bid);
			}
			if (ask->IsFilled()) 
			{
				asks.PopFront();
				orders_.erase(ask->GetOrderId());
				ask->reset();
				orderPool_.deallocate(ask);
			}

			trades.push_back(Trade{
				TradeInfo{bid->GetOrderId(),bid->GetPrice(),quantity},
				TradeInfo{ask->GetOrderId(),ask->GetPrice(),quantity }
			});

			OnOrderMatched(bid->GetPrice(), quantity, bid->IsFilled());
			OnOrderMatched(ask->GetPrice(), quantity, ask->IsFilled());
		}

		if (bids.Empty())
		{
			bids_.erase(bidPrice);
			data_.erase(bidPrice);
		}
		if (asks.Empty())
		{
			asks_.erase(askPrice);
			data_.erase(askPrice);
		}
	}
	if (!bids_.empty()) {
		auto& [_, bids] = *bids_.begin();
		auto order = bids.Front();
		if (order->GetOrderType() == OrderType::FillAndKill)
			CancelOrder(order->GetOrderId());
	}
	if (!asks_.empty()) {
		auto& [_, asks] = *asks_.begin();
		auto order = asks.Front();
		if (order->GetOrderType() == OrderType::FillAndKill)
			CancelOrder(order->GetOrderId());
	}
	return trades;
}


void Orderbook::PruneGoodForDayOrders() {
	//cancels goodforday orders at the end of the day, in this function its 4:00pm for the user's timezone
	using namespace std::chrono;
	const auto end = hours(16);

	while (true)
	{
		const auto now = system_clock::now();
		const auto now_c = system_clock::to_time_t(now);
		std::tm now_parts;
		localtime_s(&now_parts, &now_c);

		if (now_parts.tm_hour>=end.count())
		{
			now_parts.tm_mday += 1;
		}
		now_parts.tm_hour = end.count();
		now_parts.tm_min = 0;
		now_parts.tm_sec = 0;

		auto next = system_clock::from_time_t(mktime(&now_parts));
		auto till = next - now + milliseconds(100);

		{
			std::unique_lock ordersLock{ ordersMutex_ };
			if (shutdown_.load(std::memory_order_acquire) || shutdownConditionVariable_.wait_for(ordersLock, till) == std::cv_status::no_timeout)
				return;
		}

		OrderIds orderIds;

		{
			std::scoped_lock ordersLock{ ordersMutex_ };
			for (const auto& [_, entry] : orders_)
			{
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
	shutdown_.store(true, std::memory_order_release);
	shutdownConditionVariable_.notify_one();
	ordersPruneThread_.join();
}

Orderbook::Orderbook() : ordersPruneThread_{ [this] {PruneGoodForDayOrders();} }{}

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
	UpdateLevelData(price,quantity,isFullyFilled ? LevelData::Action::Remove : LevelData::Action::Match);
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
	if (data.count_ == 0)
		data_.erase(price);
}
bool Orderbook::CanFullyFill(Side side, Price price, Quantity quantity) const
{
	if (!CanMatch(side, price))
		return false;

	std::optional<Price> threshold;
	
	if (side == Side::Buy)
	{
		const auto [askPrice, _] = *asks_.begin();
		threshold = askPrice;
	}
	else
	{
		const auto [bidPrice, _] = *bids_.begin();
		threshold = bidPrice;
	}

	for (const auto& [levelPrice, levelData] : data_)
	{
		if (threshold.has_value() && (side == Side::Buy && threshold.value() > levelPrice) || (side == Side::Sell && threshold.value() < levelPrice))
			continue;
		if ((side == Side::Buy && levelPrice > price) || (side == Side::Sell && levelPrice < price))
			continue;
		if (quantity <= levelData.quantity_)
			return true;
		quantity -= levelData.quantity_;
	}
	return false;
}

OrderPointer Orderbook::AllocateOrder(OrderType type, OrderId id, Side side, Price price, Quantity quantity) {
	Order* order = orderPool_.allocate();
	if (order != nullptr) {
		order->init(type, id, side, price, quantity);
	}
	return order;
}