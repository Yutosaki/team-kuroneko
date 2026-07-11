package customdb.chapter08.Operator;

import java.io.IOException;
import java.util.Iterator;
import java.util.List;

import customdb.chapter08.DB.Row;
import customdb.chapter08.DB.Schema;
import customdb.chapter08.DB.Table;
import customdb.chapter08.Parser.Statement.Condition;

public class IndexScanOperator implements Operator {
  private final Table table;
  private final Schema schema;
  private final String tableName;
  private final Condition condition;
  private Iterator<Row> iterator;

  public IndexScanOperator(Table table, Schema schema, String tableName, Condition condition) {
    this.table = table;
    this.schema = schema;
    this.tableName = tableName;
    this.condition = condition;
  }

  @Override
  public void open() throws IOException {
    Schema.Column column = schema.getColumn(condition.left());
    Object value = condition.right();

    String columnName = column != null ? column.name() : condition.left();

    if (column != null) {
      value = parseValue(condition.right(), column);
    }

    List<Row> rows = table.searchByIndex(columnName, value);
    this.iterator = rows.iterator();
  }

  @Override
  public Row next() throws IOException {
    if (iterator != null && iterator.hasNext()) {
      Row rawRow = iterator.next();
      return qualifyRow(tableName, schema, rawRow);
    }
    return null;
  }

  @Override
  public void close() throws IOException {
    this.iterator = null;
  }

  private Row qualifyRow(String tableName, Schema schema, Row row) {
    Row qualified = new Row();
    for (Schema.Column column : schema.getColumns()) {
      String columnName = column.name();
      Object value = row.get(columnName);
      qualified.put(columnName, value);
      qualified.put(tableName + "." + columnName, value);
    }
    return qualified;
  }

  private Object parseValue(String rawValue, Schema.Column column) {
    String value = stripQuote(rawValue);
    return switch (column.type()) {
      case INTEGER -> Integer.parseInt(value);
      case FLOAT -> Float.parseFloat(value);
      case DOUBLE -> Double.parseDouble(value);
      case STRING -> value;
    };
  }

  private String stripQuote(String value) {
    if (value.length() >= 2 && value.startsWith("'") && value.endsWith("'")) {
      return value.substring(1, value.length() - 1);
    }
    return value;
  }
}
