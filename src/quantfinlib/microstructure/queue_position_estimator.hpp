// Port of com.quantfinlib.microstructure.QueuePositionEstimator: L2-only
// queue position estimation via the pro-rata cancel assumption (an
// unbiased expected shares-ahead without an L3 feed). One passive order
// per instance. Feed ordering contract: report trades via onTrade BEFORE
// the depth update that reflects them, and give onLevelResize sizes net
// of trades already reported.
#pragma once

namespace quantfinlib {

class QueuePositionEstimator {
public:
    /** Join the back of a level currently displaying levelSize shares (before our order). */
    void join(long levelSize, long ownQty);

    /** A trade at our level consumed tradedQty from the front. */
    void onTrade(long tradedQty);

    /** The level's displayed size changed to newLevelSize for a reason other than a trade. */
    void onLevelResize(long newLevelSize);

    /** Expected shares ahead of our order right now. */
    double sharesAhead() const { return ahead_; }

    /** Fill probability via QueueModel over a horizon expecting expectedTradedQty shares. */
    double fillProbability(double expectedTradedQty) const;

    /** Queue progress since joining: 0 right after join, 1 when fully drained. */
    double queueProgress() const;

    bool active() const { return active_; }
    long ownQty() const { return ownQty_; }

    /** Order left the book (filled/cancelled). */
    void close() { active_ = false; }

private:
    double ahead_ = 0;
    double aheadAtJoin_ = 0;
    double levelSize_ = 0;
    long ownQty_ = 0;
    bool active_ = false;
};

}  // namespace quantfinlib
