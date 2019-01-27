all: proxy.c
	gcc -o proxy proxy.c
	touch ipcache.txt cacheinfo.txt blacklist.txt
	mkdir www

clean:
	$(RM) proxy ipcache.txt blacklist.txt cacheinfo.txt

