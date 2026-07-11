package customdb.chapter08.Operator;

import java.io.IOException;

import customdb.chapter08.DB.Row;
import customdb.chapter08.Parser.Statement.Condition;

public class NestedLoopJoinOperator implements Operator {
  private final Operator leftChild;
  private final Operator rightChild;
  private final Condition onCondition;

  private Row currentLeftRow;

  public NestedLoopJoinOperator(Operator leftChild, Operator rightChild, Condition onCondition) {
    this.leftChild = leftChild;
    this.rightChild = rightChild;
    this.onCondition = onCondition;
  }

  @Override
  public void open() throws IOException {
    leftChild.open();
    rightChild.open();
    currentLeftRow = leftChild.next();
  }

  @Override
  public Row next() throws IOException {
    while (currentLeftRow != null) {
      Row rightRow = rightChild.next();

      if (rightRow == null) {
        rightChild.close();
        rightChild.open();
        currentLeftRow = leftChild.next();
        continue;
      }

      Row joined = new Row();
      for (String key : currentLeftRow.keySet()) {
        if (key.contains(".")) joined.put(key, currentLeftRow.get(key));
      }
      for (String key : rightRow.keySet()) {
        if (key.contains(".")) joined.put(key, rightRow.get(key));
      }

      if (ConditionEvaluator.matches(joined, onCondition)) {
        return joined;
      }
    }
    return null;
  }

  @Override
  public void close() throws IOException {
    leftChild.close();
    rightChild.close();
  }
}
