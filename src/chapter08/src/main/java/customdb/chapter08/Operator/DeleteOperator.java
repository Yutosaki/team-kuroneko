package customdb.chapter08.Operator;

import java.io.IOException;
import java.util.Iterator;

import customdb.chapter08.DB.Row;
import customdb.chapter08.DB.Table;
import customdb.chapter08.Parser.Statement;

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
