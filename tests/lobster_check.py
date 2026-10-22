"""Checks a LOBSTER message/orderbook pair independently of the C++ that wrote it: rebuilds the
book from the message file alone and compares it with every orderbook line.
Usage: python3 lobster_check.py MESSAGE.csv ORDERBOOK.csv LEVELS"""

import csv
import sys

NO_ASK, NO_BID = 9999999999, -9999999999


def main(msg_path, ob_path, levels):
    orders = {}  # id -> [price, size, dir]
    with open(msg_path) as fm, open(ob_path) as fo:
        assert sum(1 for _ in fm) == sum(1 for _ in fo), "message and orderbook files differ in length"
    depth = {1: {}, -1: {}}  # dir -> price -> shares
    lines, last = 0, 0.0
    with open(msg_path, newline="") as fm, open(ob_path, newline="") as fo:
        for msg, ob in zip(csv.reader(fm), csv.reader(fo)):
            lines += 1
            t, typ, oid, size, price, d = float(msg[0]), int(msg[1]), int(msg[2]), int(msg[3]), int(msg[4]), int(msg[5])
            assert t >= last, f"line {lines}: time goes back"
            last = t
            assert d in (1, -1) and typ in (1, 2, 3, 4, 5, 6, 7), f"line {lines}: type {typ} dir {d}"
            if typ == 1:
                assert oid not in orders, f"line {lines}: order {oid} submitted twice"
                orders[oid] = [price, size, d]
                depth[d][price] = depth[d].get(price, 0) + size
            elif typ in (2, 3, 4):
                p, left, od = orders[oid]
                assert (p, od) == (price, d), f"line {lines}: order {oid} price or side changed"
                assert size <= left, f"line {lines}: removes {size} of {left}"
                assert typ != 3 or size == left, f"line {lines}: deletion of {size}, {left} left"
                depth[d][price] -= size
                if not depth[d][price]:
                    del depth[d][price]
                orders[oid][1] -= size
                if not orders[oid][1]:
                    del orders[oid]
            want = []
            asks, bids = sorted(depth[-1].items()), sorted(depth[1].items(), reverse=True)
            for m in range(levels):
                want += list(asks[m]) if m < len(asks) else [NO_ASK, 0]
                want += list(bids[m]) if m < len(bids) else [NO_BID, 0]
            got = [int(x) for x in ob]
            assert got == want, f"line {lines}: book {got[:4]}... differs from the messages' {want[:4]}..."
    return lines


if __name__ == "__main__":
    n = main(sys.argv[1], sys.argv[2], int(sys.argv[3]))
    assert n > 0, "no events"
    print(f"lobster ok: {n} events, every book line matches the messages")
