#pragma once

#include <embedded/embedded.h>

#include <filesystem>
#include <string>

#ifndef _WIN32
#define LLFMT "%lld"
#else
#define LLFMT "%I64d"
#endif

static void printResult(monetdb_result* result, unsigned int maxRows = (unsigned int)(-1)) {
  for(size_t r = 0; r < result->nrows && r < maxRows; r++) {
    for(size_t c = 0; c < result->ncols; c++) {
      monetdb_column* actual_column = monetdb_result_fetch(result, c);
      switch(actual_column->type) {
      case monetdb_int8_t: {
        monetdb_column_int8_t* col = (monetdb_column_int8_t*)actual_column;
        printf("%d", (int)col->data[r]);
        break;
      }
      case monetdb_int16_t: {
        monetdb_column_int16_t* col = (monetdb_column_int16_t*)actual_column;
        printf("%d", (int)col->data[r]);
        break;
      }
      case monetdb_int32_t: {
        monetdb_column_int32_t* col = (monetdb_column_int32_t*)actual_column;
        printf("%d", (int)col->data[r]);
        break;
      }
      case monetdb_int64_t: {
        monetdb_column_int64_t* col = (monetdb_column_int64_t*)actual_column;
        printf(LLFMT, (long long int)col->data[r]);
        break;
      }
      case monetdb_float: {
        monetdb_column_float* col = (monetdb_column_float*)actual_column;
        printf("%f", col->data[r]);
        break;
      }
      case monetdb_double: {
        monetdb_column_double* col = (monetdb_column_double*)actual_column;
        printf("%lf", col->data[r]);
        break;
      }
      case monetdb_str: {
        monetdb_column_str* col = (monetdb_column_str*)actual_column;
        printf("%s", col->data[r] ? col->data[r] : "NULL");
        break;
      }
      default: {
        printf("UNKNOWN");
      }
      }

      if(c + 1 < result->ncols) {
        printf(", ");
      }
    }
    printf("\n");
  }
}

class MonetDBHandling {
public:
  MonetDBHandling(int size, bool lineitemOnly) {
#ifdef WIN32
    system("rd /s /q \"./monetdbfarm\""); // make sure it is a clean folder
#else
    system("rm -rf ./monetdbfarm"); // make sure it is a clean folder
#endif
    auto absFarmPath = std::filesystem::absolute("monetdbfarm").string();
    auto error = monetdb_startup(absFarmPath.data(), 0, 0);
    if(error != 0) {
      throw std::runtime_error("MonetDB Init failed: " + std::string(error));
    }

    connection = monetdb_connect();

    monetdb_result* result = 0;
    std::string startTransaction = "START TRANSACTION;";
    error = monetdb_query(connection, startTransaction.data(), 1, NULL, NULL, NULL);
    if(error != 0) {
      throw std::runtime_error("MonetDB start transaction failed: " + std::string(error));
    }
    std::vector<std::string> tables{"region",   "nation",   "part",   "supplier",
                                    "partsupp", "customer", "orders", "lineitem"};
    std::vector<std::string> createCmds{
        " CREATE TABLE region  ( r_regionkey  INT NOT NULL,"
        "                             r_name       CHAR(25) NOT NULL,"
        "                             r_comment    VARCHAR(152));",
        " CREATE TABLE nation  ( n_nationkey  INT NOT NULL,"
        "                             n_name       CHAR(25) NOT NULL,"
        "                             n_regionkey  INT NOT NULL,"
        "                             n_comment    VARCHAR(152));",
        " CREATE TABLE part  ( p_partkey     INT NOT NULL,"
        "                           p_name        VARCHAR(55) NOT NULL,"
        "                           p_mfgr        CHAR(25) NOT NULL,"
        "                           p_brand       CHAR(10) NOT NULL,"
        "                           p_type        VARCHAR(25) NOT NULL,"
        "                           p_size        INT NOT NULL,"
        "                           p_container   CHAR(10) NOT NULL,"
        "                           p_retailprice DECIMAL(15,2) NOT NULL,"
        "                           p_comment     VARCHAR(23) NOT NULL);",
        " CREATE TABLE supplier ( s_suppkey     INT NOT NULL,"
        "                              s_name        CHAR(25) NOT NULL,"
        "                              s_address     VARCHAR(40) NOT NULL,"
        "                              s_nationkey   INT NOT NULL,"
        "                              s_phone       CHAR(15) NOT NULL,"
        "                              s_acctbal     DECIMAL(15,2) NOT NULL,"
        "                              s_comment     VARCHAR(101) NOT NULL);",
        " CREATE TABLE partsupp ( Ps_partkey     INT NOT NULL,"
        "                              Ps_suppkey     INT NOT NULL,"
        "                              Ps_availqty    INT NOT NULL,"
        "                              Ps_supplycost  DECIMAL(15,2)  NOT NULL,"
        "                              Ps_comment     VARCHAR(199) NOT NULL);",
        " CREATE TABLE customer ( c_custkey     INT NOT NULL,"
        "                              c_name        VARCHAR(25) NOT NULL,"
        "                              c_address     VARCHAR(40) NOT NULL,"
        "                              c_nationkey   INT NOT NULL,"
        "                              c_phone       CHAR(15) NOT NULL,"
        "                              c_acctbal     DECIMAL(15,2)   NOT NULL,"
        "                              c_mktsegment  CHAR(10) NOT NULL,"
        "                              c_comment     VARCHAR(117) NOT NULL);",
        " CREATE TABLE orders  ( o_orderkey       INT NOT NULL,"
        "                            o_custkey        INT NOT NULL,"
        "                            o_orderstatus    CHAR(1) NOT NULL,"
        "                            o_totalprice     DECIMAL(15,2) NOT NULL,"
        "                            o_orderdate      DATE NOT NULL,"
        "                            o_orderpriority  CHAR(15) NOT NULL,  "
        "                            o_clerk          CHAR(15) NOT NULL, "
        "                            o_shippriority   INT NOT NULL,"
        "                            o_comment        VARCHAR(79) NOT NULL);",
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
        "                              l_comment      VARCHAR(44) NOT NULL);"};
    for(size_t i = lineitemOnly ? tables.size() - 1 : 0; i < tables.size(); ++i) {
      error = monetdb_query(connection, createCmds[i].data(), 1, NULL, NULL, NULL);
      if(error != 0) {
        throw std::runtime_error("MonetDB create table failed: " + std::string(error));
      }
      std::string path = "../data/tpch_" + std::to_string(size) + "MB/" + tables[i] + ".tbl";
      auto absFilepath = std::filesystem::absolute(path).string();
      auto loadingQuery =
          "COPY INTO " + tables[i] + " FROM '" + absFilepath + "' USING DELIMITERS '|', '\n';";
      error = monetdb_query(connection, loadingQuery.data(), 1, NULL, NULL, NULL);
      if(error != 0) {
        throw std::runtime_error("MonetDB file loading failed: " + std::string(error));
      }
    }
  }

  ~MonetDBHandling() {
    monetdb_disconnect(connection);
    monetdb_shutdown();
  }

  monetdb_connection& getConnection() { return connection; }

private:
  monetdb_connection connection;
};

static auto& MonetDBhandlingPtr() {
  static std::unique_ptr<MonetDBHandling> monetDBhandling;
  return monetDBhandling;
}

static void releaseMonetDB() { MonetDBhandlingPtr().reset(); }

static auto& initMonetDB(size_t size, bool lineitemOnly = false) {
  static auto lastSize = size;
  static auto lastLineitemOnly = lineitemOnly;
  if(!MonetDBhandlingPtr() || size != lastSize || lastLineitemOnly != lineitemOnly) {
    lastSize = size;
    lastLineitemOnly = lineitemOnly;
    MonetDBhandlingPtr().reset();
    MonetDBhandlingPtr() = std::make_unique<MonetDBHandling>(size, lineitemOnly);
  }
  lastSize = size;
  return MonetDBhandlingPtr()->getConnection();
}
