
#ifndef NULLSERVER_H
#define NULLSERVER_H

int nullserver_create(const char* ip, const char* port);
void nullserver_destroy();
int nullserver_exec();
void nullserver_quit();

#endif // NULLSERVER_H
