#!/bin/sh

# change for your monitor (wlr_randr)
wlr-randr --output DP-1 --pos 0,0 --output HDMI-A-1 --pos 1366,0

# Указываем точный путь к скомпилированному оконному менеджеру
# Запускаем в фоне, чтобы скрипт не блокировался
/home/yaso/Desktop/SDRl/river-wm-client &