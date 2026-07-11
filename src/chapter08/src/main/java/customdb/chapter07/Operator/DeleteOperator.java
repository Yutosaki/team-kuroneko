package customdb.chapter07.Operator;

import customdb.chapter07.DB.Row;
import customdb.chapter07.DB.Table;
import customdb.chapter07.Parser.Statement;
import java.io.IOException;
import java.util.Iterator;

public class DeleteOperator implements Operator {
  private final Table table;
  private final Statement.Delete statement;
  private Iterator<Table.Record> iterator;

  public DeleteOperator(Table table, Statement.Delete statement) {
    this.table = table;
    this.statement = statement;
  }

  @Override
  public void open() throws IOException {
    this.iterator = table.scanRecords().iterator();
  }

  @Override
  public Row next() throws IOException {
    while (iterator != null && iterator.hasNext()) {
      Table.Record record = iterator.next();
      Row row = record.row();

      if (ConditionEvaluator.matches(row, statement.whereCondition())) {
        table.delete(record.recordId());
        return row;
      }
    }

    return null;
  }

  @Override
  public void close() throws IOException {
    this.iterator = null;
  }
}
