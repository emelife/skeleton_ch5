#!/bin/bash
modprobe media
modprobe videodev
modprobe v4l2-common
modprobe tuner
modprobe videobuf2-core
modprobe v4l2-mem2mem
modprobe videobuf2-memops
modprobe videobuf2-dma-sg
modprobe videobuf2-dma-contig
modprobe videobuf2-vmalloc
modprobe videobuf-core
modprobe dvb-core
modprobe videobuf2-dvb
modprobe videobuf-dvb
modprobe videobuf-vmalloc
modprobe videobuf-dma-sg
modprobe v4l2-dv-timings
