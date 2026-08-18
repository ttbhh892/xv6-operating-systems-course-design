#!/bin/bash

labs="util syscall pgtbl traps cow thread net lock fs mmap"
logdir="$HOME/os-labs/xv6-grade-logs"

mkdir -p "$logdir"

for lab in $labs
do
    echo
    echo "========================================"
    echo "开始测试：$lab"
    echo "========================================"

    # 结束可能残留的 QEMU
    pkill -f '[q]emu-system-riscv64' 2>/dev/null || true

    # 切换实验分支
    if ! git switch "$lab"; then
        echo "$lab：切换分支失败，停止测试"
        break
    fi

    # 清理后进行完整评分
    make -s clean
    make -s grade 2>&1 | tee "$logdir/$lab.log"

    result=${PIPESTATUS[0]}

    if [ "$result" -eq 0 ]; then
        echo "$lab：评分程序正常完成"
    else
        echo "$lab：评分失败，退出码为 $result"
    fi

    # net 实验会修改抓包文件，恢复后才能切换分支
    if [ "$lab" = "net" ]; then
        git restore -- packets.pcap 2>/dev/null || true
    fi
done

echo
echo "========================================"
echo "十个实验测试结束"
echo "日志目录：$logdir"
echo "========================================"

grep -H "Score:" "$logdir"/*.log
