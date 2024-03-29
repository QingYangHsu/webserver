server: main.cpp threadpool.h ./http/http_conn.cpp ./http/http_conn.h locker.h ./log/log.cpp ./log/log.h ./log/block_queue.h  ./lst_timer/lst_timer.h ./lst_timer/lst_timer.cpp ./CGImysql/sql_connection_pool.cpp ./CGImysql/sql_connection_pool.h
	g++ -o server main.cpp threadpool.h ./http/http_conn.cpp ./http/http_conn.h locker.h ./log/log.cpp ./log/log.h ./log/block_queue.h  ./lst_timer/lst_timer.h ./lst_timer/lst_timer.cpp ./CGImysql/sql_connection_pool.cpp ./CGImysql/sql_connection_pool.h -lpthread -lmysqlclient


clean:
	rm  -r server
