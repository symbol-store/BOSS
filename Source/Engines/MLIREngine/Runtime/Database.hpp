#include <map>
#include <string>
#include <tuple>
#include <vector>

enum class SymbolArgumentType;

struct Buffer {
  int size;
  void* data;
};

class Table {
public:
  int getNumColumns() { return columnTypes.size(); }
  SymbolArgumentType getTypeForColumn(std::string& name);

  std::vector<Buffer>::iterator buffersBegin() { return buffers.begin(); }
  std::vector<Buffer>::iterator buffersEnd() { return buffers.end(); }

  using schema = std::vector<std::tuple<std::string, SymbolArgumentType>>;

  schema::iterator schemaBegin() { return columnTypes.begin(); }
  schema::iterator schemaEnd() { return columnTypes.end(); }

  Buffer getLastBuffer() { return *buffers.rbegin(); }
  void createNewBuffer();

  schema& getSchema() { return columnTypes; }

private:
  // Schema
  schema columnTypes;

  // All memory areas
  std::vector<Buffer> buffers;
};

class Database {
  Table getTable(std::string name);

  void createTable(std::string name, Table::schema&& schema);

private:
  std::map<std::string, Table> tables;
};
