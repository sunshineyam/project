#include "IMServer.hpp"
using namespace std;
int main()
{
    //MysqlClient *mc = new MysqlClient();
    //mc->InsertUser("老陈","1234");
    //delete mc;
    IMServer* IM = new IMServer();
    IM->InitServer();
    IM->Start();
    return 0;
}
