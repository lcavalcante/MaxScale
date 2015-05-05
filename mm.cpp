/**
 * @file mm test of multi master monitor
 */

#include <my_config.h>
#include <iostream>
#include "testconnections.h"
#include "maxadmin_operations.h"
#include "sql_t1.h"

int check_conf(TestConnections* Test, int blocked_node)
{
    int global_result;

    Test->repl->connect();
    Test->connect_rwsplit();
    create_t1(Test->conn_rwsplit);
    global_result += insert_into_t1(Test->conn_rwsplit, 4);
    printf("Sleeping to let replication happen\n"); fflush(stdout);
    sleep(30);

    for (int i = 0; i < 2; i++) {
        if ( i != blocked_node) {
            printf("Checking data from node %d (%s)\n", i, Test->repl->IP[i]); fflush(stdout);
            global_result += select_from_t1(Test->repl->nodes[i], 4);
        }
    }

    printf("Checking data from rwsplit\n"); fflush(stdout);
    global_result += select_from_t1(Test->conn_rwsplit, 4);
    global_result += execute_query(Test->conn_rwsplit, "DROP TABLE t1");

    Test->repl->close_connections();
    mysql_close(Test->conn_rwsplit);

    return(global_result);
}

int main(int argc, char *argv[])
{
    TestConnections * Test = new TestConnections(argc, argv);
    int global_result = 0;
    char maxadmin_result[1024];

    Test->read_env();
    Test->print_env();

    Test->repl->set_repl_user();

    Test->start_mm(); // first node - slave, second - master

    get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show server server1", (char *) "Status:", maxadmin_result);
    printf("node0 %s+n", maxadmin_result);
    if (strcmp(maxadmin_result, "Slave, Running")) {
        printf("Node0 is not slave, status is %s\n", maxadmin_result);
        global_result++;
    }
    get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show server server2", (char *) "Status:", maxadmin_result);
    printf("node1 %s+n", maxadmin_result);
    if (strcmp(maxadmin_result, "Master, Running")) {
        printf("Node1 is not master, status is %s\n", maxadmin_result);
        global_result++;
    }

    printf("Put some data and check\n");
    global_result += check_conf(Test, 2);

    printf("Block slave\n");
    Test->repl->block_node(0);
    sleep(30);
    printf("Put some data and check\n");
    global_result += check_conf(Test, 0);

    printf("Unlock slave\n");
    Test->repl->unblock_node(0);
    sleep(30);

    printf("Block master\n");
    Test->repl->block_node(1);
    sleep(30);
    printf("Make node 1 master\n");

    Test->repl->connect();
    execute_query(Test->repl->nodes[0], (char *) "SET GLOBAL READ_ONLY=OFF");
    Test->repl->close_connections();

    sleep(30);
    printf("Put some data and check\n");
    global_result += check_conf(Test, 1);

    printf("Unlock slave\n");
    Test->repl->unblock_node(1);
    sleep(30);

    printf("Make node 2 slave\n");
    Test->repl->connect();
    execute_query(Test->repl->nodes[1], (char *) "SET GLOBAL READ_ONLY=ON");
    Test->repl->close_connections();
    sleep(30);

    printf("Put some data and check\n");
    global_result += check_conf(Test, 2);

    get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show server server2", (char *) "Status:", maxadmin_result);
    printf("node1 %s+n", maxadmin_result);
    if (strcmp(maxadmin_result, "Slave, Running")) {
        printf("Node1 is not slave, status is %s\n", maxadmin_result);
        global_result++;
    }
    get_maxadmin_param(Test->maxscale_IP, (char *) "admin", Test->maxadmin_password, (char *) "show server server1", (char *) "Status:", maxadmin_result);
    printf("node0 %s+n", maxadmin_result);
    if (strcmp(maxadmin_result, "Master, Running")) {
        printf("Node0 is not master, status is %s\n", maxadmin_result);
        global_result++;
    }

    Test->copy_all_logs(); return(global_result);
}
