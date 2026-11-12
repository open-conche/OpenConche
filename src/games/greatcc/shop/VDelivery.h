#pragma once

// VDelivery (docs/re/greatcc-machines-and-chocolates.md 5.7): the zeppelin's order box. After an interval an order shows in a thought
// bubble over the station; she brings a matching chocolate before its patience runs out, the machine boxes it, and the zeppelin
// comes for the box. The order is paid when it is boxed. The level may not end while a delivery is under way.

#include <list>

#include "games/greatcc/core/LevelData.h"
#include "games/greatcc/shop/BZeppelin.h"
#include "games/greatcc/shop/ThoughtBubble.h"
#include "games/greatcc/shop/VMachine.h"

namespace choco::games::greatcc {

class VDelivery : public VMachine {
 public:
  // The state at +0x2b8.
  enum DeliveryState { kWaitingForOrder = 0, kOrderShowing = 1, kBoxing = 2, kBoxed = 3 };

  explicit VDelivery(script::ScriptContext& ctx);
  const char* kind() const override { return "VDelivery"; }

  // The airship (made once, added to the shop's scene) and the thought bubble from the `bubble` style (a child of the machine);
  // then VMachine::LoadFromTable.
  void loadFromTable(const StyleChain& style) override;
  // Enabled by `kDelivery1`. The first Init (when enabled or in endless mode) starts over: no orders, nothing wanted, the airship and
  // the bubble initialised, and a story level's zeppelin orders are copied in; then VMachine::Init. Shown only when enabled.
  void init() override;
  // VMachine::Reset, the base frame 1, the bubble away (unless it is fading), nothing wanted, patience 100, mood 1, waiting for an
  // order, and the next interval: `delivery_interval` plus a draw in [-V/2, V/2) of `delivery_variance`.
  void reset() override;
  // The boxed chocolate hides once the box animation is past its 58th frame; the countdown to the next order (not while softly paused);
  // the bubble's fade; the order's patience and mood; the level is told whether it may end. Then VMachine::Update.
  void update(bool softPaused) override;
  // With an order showing (and the machine not waiting): the carried chocolate that equals it, hand A first, goes into the box.
  void onArrival(VPlayer& player) override;
  // The boxing animations play (the machine waits for the back one), the airship is called, `box_station.ogg`.
  void activate() override;
  // The box is done: the chocolate leaves, the airship is told the box is ready, `num_deliveries` counts.
  bool onResume() override;

  // SetMood: the bubble shows the art of the mood.
  void setMood(int mood);
  // AttachThought(fadeIn): the bubble fades in with the wanted chocolate in it (and the tutorial check), or fades out.
  void attachThought(bool fadeIn);
  // GenerateOrder: the next scripted order, else a random chocolate; nothing while something is wanted.
  void generateOrder();

  int deliveryState() const { return deliveryState_; }
  int timerMs() const { return timer_; }
  void setTimerMs(int ms) { timer_ = ms; }
  float patience() const { return patience_; }
  void setPatience(float p) { patience_ = p; }
  int mood() const { return mood_; }
  const std::shared_ptr<VChocolate>& wanted() const { return wanted_; }
  const std::shared_ptr<BZeppelin>& zeppelin() const { return zeppelin_; }
  const std::shared_ptr<VThoughtBubble>& bubble() const { return bubble_; }
  const std::list<OrderInfo>& orders() const { return orders_; }

 private:
  std::shared_ptr<VChocolate> wanted_;      // +0x27c
  std::shared_ptr<VThoughtBubble> bubble_;  // +0x284
  int timer_ = 0;                           // +0x28c: ms to the next order
  std::shared_ptr<AlphaEffect> fade_;       // +0x290
  int mood_ = 1;                            // +0x298
  float patience_ = 100.0f;                 // +0x29c
  std::shared_ptr<BZeppelin> zeppelin_;     // +0x2a0
  std::list<OrderInfo> orders_;             // +0x2b0
  int deliveryState_ = kWaitingForOrder;    // +0x2b8
};

}  // namespace choco::games::greatcc
