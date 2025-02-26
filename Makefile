.SUFFIXES: .c .o
CC = gcc
CCFLAGS = -g -Wall
DEPS = myftp.h
EXEC = myftpserve myftp
OBJS_SERVER = myftpserve.o
OBJS_CLIENT = myftp.o

all: ${EXEC}

myftpserve: ${OBJS_SERVER}
	${CC} ${CCFLAGS} -o myftpserve ${OBJS_SERVER}

myftp: ${OBJS_CLIENT}
	${CC} ${CCFLAGS} -o myftp ${OBJS_CLIENT}

%.o: %.c ${DEPS}
	${CC} ${CCFLAGS} -c $<

clean:
	rm -f ${EXEC} *.o