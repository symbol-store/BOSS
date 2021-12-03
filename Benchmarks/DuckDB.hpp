#pragma once
#include <duckdb.hpp>

class DuckDBHandling {
public:
  DuckDBHandling(size_t size, bool lineitemOnly) : db(nullptr), connection(db) {
    std::vector<std::string> tables{"region",   "nation",   "part",   "supplier",
                                    "partsupp", "customer", "orders", "lineitem"};
    std::vector<std::string> createCmds{
        " CREATE TABLE region  ( r_regionkey  INT NOT NULL,"
        "                             r_name       CHAR(25) NOT NULL,"
        "                             r_comment    VARCHAR(152),"
        "                              dummy           VARCHAR);",
        " CREATE TABLE nation  ( n_nationkey  INT NOT NULL,"
        "                             n_name       CHAR(25) NOT NULL,"
        "                             n_regionkey  INT NOT NULL,"
        "                             n_comment    VARCHAR(152),"
        "                              dummy           VARCHAR);",
        " CREATE TABLE part  ( p_partkey     INT NOT NULL,"
        "                           p_name        VARCHAR(55) NOT NULL,"
        "                           p_mfgr        CHAR(25) NOT NULL,"
        "                           p_brand       CHAR(10) NOT NULL,"
        "                           p_type        VARCHAR(25) NOT NULL,"
        "                           p_size        INT NOT NULL,"
        "                           p_container   CHAR(10) NOT NULL,"
        "                           p_retailprice DECIMAL(15,2) NOT NULL,"
        "                           p_comment     VARCHAR(23) NOT NULL,"
        "                              dummy           VARCHAR);",
        " CREATE TABLE supplier ( s_suppkey     INT NOT NULL,"
        "                              s_name        CHAR(25) NOT NULL,"
        "                              s_address     VARCHAR(40) NOT NULL,"
        "                              s_nationkey   INT NOT NULL,"
        "                              s_phone       CHAR(15) NOT NULL,"
        "                              s_acctbal     DECIMAL(15,2) NOT NULL,"
        "                              s_comment     VARCHAR(101) NOT NULL,"
        "                              dummy           VARCHAR);",
        " CREATE TABLE partsupp ( Ps_partkey     INT NOT NULL,"
        "                              Ps_suppkey     INT NOT NULL,"
        "                              Ps_availqty    INT NOT NULL,"
        "                              Ps_supplycost  DECIMAL(15,2)  NOT NULL,"
        "                              Ps_comment     VARCHAR(199) NOT NULL,"
        "                              dummy           VARCHAR);",
        " CREATE TABLE customer ( c_custkey     INT NOT NULL,"
        "                              c_name        VARCHAR(25) NOT NULL,"
        "                              c_address     VARCHAR(40) NOT NULL,"
        "                              c_nationkey   INT NOT NULL,"
        "                              c_phone       CHAR(15) NOT NULL,"
        "                              c_acctbal     DECIMAL(15,2)   NOT NULL,"
        "                              c_mktsegment  CHAR(10) NOT NULL,"
        "                              c_comment     VARCHAR(117) NOT NULL,"
        "                              dummy           VARCHAR);",
        " CREATE TABLE orders  ( o_orderkey       INT NOT NULL,"
        "                            o_custkey        INT NOT NULL,"
        "                            o_orderstatus    CHAR(1) NOT NULL,"
        "                            o_totalprice     DECIMAL(15,2) NOT NULL,"
        "                            o_orderdate      DATE NOT NULL,"
        "                            o_orderpriority  CHAR(15) NOT NULL,  "
        "                            o_clerk          CHAR(15) NOT NULL, "
        "                            o_shippriority   INT NOT NULL,"
        "                            o_comment        VARCHAR(79) NOT NULL,"
        "                              dummy           VARCHAR);",
        " CREATE TABLE lineitem ( l_orderkey    INT NOT NULL,"
        "                              l_partkey     INT NOT NULL,"
        "                              l_suppkey     INT NOT NULL,"
        "                              l_linenumber  INT NOT NULL,"
        "                              l_quantity    DECIMAL(15,2) NOT NULL,"
        "                              l_extendedprice  DECIMAL(15,2) NOT NULL,"
        "                              l_discount    DECIMAL(15,2) NOT NULL,"
        "                              l_tax         DECIMAL(15,2) NOT NULL,"
        "                              l_returnflag  CHAR(1) NOT NULL,"
        "                              l_linestatus  CHAR(1) NOT NULL,"
        "                              l_shipdate    DATE NOT NULL,"
        "                              l_commitdate  DATE NOT NULL,"
        "                              l_receiptdate DATE NOT NULL,"
        "                              l_shipinstruct CHAR(25) NOT NULL,"
        "                              l_shipmode     CHAR(10) NOT NULL,"
        "                              l_comment      VARCHAR(44) NOT NULL,"
        "                              dummy           VARCHAR);"};
    for(int i = lineitemOnly ? tables.size() - 1 : 0; i < tables.size(); ++i) {
      auto const& table = tables[i];
      auto result = connection.Query(createCmds[i]);
      if(!result->success) {
        result->Print();
      }
      std::string path = "../data/tpch_" + std::to_string(size) + "MB/" + table + ".tbl";
      auto insertCmd = "INSERT INTO " + table + " SELECT * FROM read_csv_auto('" + path + "');";
      result = connection.Query(insertCmd);
      if(!result->success) {
        result->Print();
      }
    }
  }

  ~DuckDBHandling() {}

  duckdb::Connection& getConnection() { return connection; }

private:
  duckdb::DuckDB db;
  duckdb::Connection connection;
};

static auto& duckDBHandlingPtr() {
  static std::unique_ptr<DuckDBHandling> duckDBhandling;
  return duckDBhandling;
}

static void releaseDuckDB() { duckDBHandlingPtr().reset(); }

static auto& initDuckDB(size_t size, bool lineitemOnly = false) {
  static auto lastSize = size;
  static auto lastLineitemOnly = lineitemOnly;
  if(!duckDBHandlingPtr() || size != lastSize || lastLineitemOnly != lineitemOnly) {
    lastSize = size;
    lastLineitemOnly = lineitemOnly;
    duckDBHandlingPtr().reset();
    duckDBHandlingPtr() = std::make_unique<DuckDBHandling>(size, lineitemOnly);
  }
  lastSize = size;
  return duckDBHandlingPtr()->getConnection();
}
