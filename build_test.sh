#!/bin/bash


# By defualt, if a command exits before the timeout duration expires, the exit
# status of the command is returned by timeout.
# 
# By default, if a command exceeds the timeout duration and is killed by timeout
# the exist status 124 is returned by timeout.
# 
# When --preserve-status is used, if a command exits before the timeout duration
# expires, the exit status of the command is returned by timeout.
# 
# When --preserve-status is used, if a command exceeds the timeout duration and
# is killed by timeout, the exist status returned by the command is 128 + N,
# where N is the signal number it received from timeout. Timeout returns this
# exit code. By default timeout sends the SIGTERM signal to the command. SIGTERM
# is signal number 15 (as defined in /usr/include/asm-generic/signal.h), meaning
# the exit code is 143 in this case.


gcc -o build/etherate_mt src/main.c -lpthread -Wall -Werror -pedantic -ftrapv -O0 -g --std=c11 -Wjump-misses-init -Wlogical-op -Wshadow -Wformat=2 -Wformat-signedness -Wextra -Wdouble-promotion -Winit-self -Wtrampolines -Wcast-qual -Wcast-align -Wwrite-strings

if [ $? -ne 0 ]
then
    echo "Compile failed"
    exit 1
fi

sudo ./build/etherate_mt -l

if [ $? -ne 0 ]
then
    echo "Execute failed"
    exit 1
fi



gcc -o build/etherate_mt src/main.c -pthread -Wall -Werror -O0 -g -fsanitize=leak -fsanitize=address

if [ $? -ne 0 ]
then
    echo "Compile failed with AddressSanitizer"
    exit 1
fi

sudo timeout --preserve-status 5 ./build/etherate_mt -I 3 -x

if [ $? -ne 143 ]
then
    echo "Execute failed with AddressSanitizer"
    exit 1
fi



gcc -o build/etherate_mt src/main.c -pthread -Wall -Werror -O0 -g -fsanitize=alignment -fsanitize=thread

if [ $? -ne 0 ]
then
    echo "Compile failed with ThreadSanitizer"
    exit 1
fi

sudo timeout --preserve-status 5 ./build/etherate_mt -I 3 -x

if [ $? -ne 143 ]
then
    echo "Execute failed with ThreadSanitizer"
    exit 1
fi



gcc -o build/etherate_mt src/main.c -lpthread -O3 --std=c11


if [ $? -ne 0 ]
then
    echo "Optimised compile failed"
    exit 1
fi

sudo ./build/etherate_mt -l

if [ $? -ne 0 ]
then
    echo "Optimised execute failed"
    exit 1
fi
