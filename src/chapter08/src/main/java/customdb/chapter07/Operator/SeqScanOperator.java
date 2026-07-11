package customdb.chapter07.Operator;

import customdb.chapter07.DB.Row;
import customdb.chapter07.DB.Schema;
import customdb.chapter07.DB.Table;
import java.io.IOException;
import java.util.Iterator;
import java.util.List;

public class SeqScanOperator implements Operator {
  private final Table table;
  private final Schema schema;
  private final String tableName;
  private Iterator<Row> iterator;

  public SeqScanOperator(Table table, Schema schema, String tableName) {
    this.table = table;
    this.schema = schema;
    this.tableName = tableName;
  }

  @Override
  public void open() throws IOException {
    List<Row> rows = table.scan();
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
}
