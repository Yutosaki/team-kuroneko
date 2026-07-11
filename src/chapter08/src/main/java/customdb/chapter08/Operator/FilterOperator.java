package customdb.chapter08.Operator;

import java.io.IOException;

import customdb.chapter08.DB.Row;
import customdb.chapter08.Parser.Statement.Condition;

public class FilterOperator implements Operator {
  private final Operator child;
  private final Condition condition;

  public FilterOperator(Operator child, Condition condition) {
    this.child = child;
    this.condition = condition;
  }

  @Override
  public void open() throws IOException {
    child.open();
  }

  @Override
  public Row next() throws IOException {
    while (true) {
      Row row = child.next();
      if (row == null) {
        return null;
      }
      if (ConditionEvaluator.matches(row, condition)) {
        return row;
      }
    }
  }

  @Override
  public void close() throws IOException {
    child.close();
  }
}
