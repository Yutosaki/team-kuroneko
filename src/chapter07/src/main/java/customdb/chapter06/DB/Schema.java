package customdb.chapter06.DB;

import java.util.List;

public class Schema {
  private final String tableName;
  private final List<Column> columns;
  private static final int RECORD_HEADER_SIZE = 1;

  public Schema(String tableName, List<Column> columns) {
    this.tableName = tableName;
    this.columns = columns;
  }

  public String getTableName() {
    return tableName;
  }

  public List<Column> getColumns() {
    return columns;
  }

  public Column getColumn(String columnName) {
    for (Column column : columns) {
      if (column.name().equalsIgnoreCase(columnName)) {
        return column;
      }
    }
    return null;
  }

  public int getRecordSize() {
    int size = RECORD_HEADER_SIZE;

    for (Column column : columns) {
      size += column.size();
    }

    return size;
  }

  public int getMaxSlots(int pageSize) {
    return pageSize / getRecordSize();
  }

  public record Column(String name, DataType type, int length, boolean indexed) {

    public int size() {
      return switch (type) {
        case INTEGER -> 4;
        case FLOAT -> 4;
        case DOUBLE -> 8;
        case STRING -> 4 + length;
      };
    }

    public boolean isIndexed() {
      return indexed;
    }
  }

  public enum DataType {
    INTEGER,
    STRING,
    FLOAT,
    DOUBLE
  }
}
