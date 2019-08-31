cc=g++
bin=IMServer
src=IMServer.cc mongoose/mongoose.c
INCLUDE=-Imongoose/ -Imysql/include
LIB=-Lmysql/lib 
LIB_NAME=-lmysqlclient -ljsoncpp
$(bin):$(src)
	$(cc) -o $@ $^ -std=c++11 $(INCLUDE) $(LIB) $(LIB_NAME)

.PHONY:clean
clean:
	rm -f $(bin)
