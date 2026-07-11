package customdb.chapter08.Planner;

import customdb.chapter08.DB.Catalog;
import customdb.chapter08.DB.Schema;
import customdb.chapter08.DB.Table;
import customdb.chapter08.Operator.DeleteOperator;
import customdb.chapter08.Operator.FilterOperator;
import customdb.chapter08.Operator.IndexScanOperator;
import customdb.chapter08.Operator.InsertOperator;
import customdb.chapter08.Operator.NestedLoopJoinOperator;
import customdb.chapter08.Operator.Operator;
import customdb.chapter08.Operator.ProjectOperator;
import customdb.chapter08.Operator.SeqScanOperator;
import customdb.chapter08.Operator.UpdateOperator;
import customdb.chapter08.Parser.Statement;

public class Planner {
  private final Catalog catalog;

  public Planner(Catalog catalog) {
    this.catalog = catalog;
  }

  public Operator createPlan(Statement statement) {
    if (statement instanceof Statement.Select selectStmt) {
      return createSelectPlan(selectStmt);
    } else if (statement instanceof Statement.Insert insertStmt) {
      return createInsertPlan(insertStmt);
    } else if (statement instanceof Statement.Update updateStmt) {
      return createUpdatePlan(updateStmt);
    } else if (statement instanceof Statement.Delete deleteStmt) {
      return createDeletePlan(deleteStmt);
    } else {
      throw new IllegalArgumentException("Unsupported statement for planning.");
    }
  }

  private Operator createSelectPlan(Statement.Select statement) {
    Table table = catalog.requireTable(statement.tableName());
    Schema schema = catalog.requireSchema(statement.tableName());
    Statement.Condition condition = statement.whereCondition();

    Operator plan;

    if (condition != null && isIndexable(schema, condition)) {
      plan = new IndexScanOperator(table, schema, statement.tableName(), condition);
    } else {
      plan = new SeqScanOperator(table, schema, statement.tableName());
    }

    boolean hasJoin = statement.joinClause() != null;

    if (!hasJoin) {
      if (condition != null) {
        plan = new FilterOperator(plan, condition);
      }
      return new ProjectOperator(plan, statement.selectColumns(), false);
    }

    String rightTableName = statement.joinClause().tableName();
    Table rightTable = catalog.requireTable(rightTableName);
    Schema rightSchema = catalog.requireSchema(rightTableName);

    // WHERE が左テーブルの列だけを見ているなら結合する前に絞り込む
    boolean pushDown =
        condition != null
            && canPushDownToLeft(
                condition, schema, statement.tableName(), rightSchema, rightTableName);

    if (pushDown) {
      plan = new FilterOperator(plan, condition);
    }

    Operator rightScan = new SeqScanOperator(rightTable, rightSchema, rightTableName);
    plan = new NestedLoopJoinOperator(plan, rightScan, statement.joinClause().onCondition());

    // 右テーブルの列を参照する条件は結合後にしか評価できない
    if (condition != null && !pushDown) {
      plan = new FilterOperator(plan, condition);
    }

    return new ProjectOperator(plan, statement.selectColumns(), true);
  }

  private boolean isIndexable(Schema schema, Statement.Condition condition) {
    Schema.Column column = schema.getColumn(condition.left());
    return column != null
        && column.isIndexed()
        && column.type() == Schema.DataType.INTEGER
        && condition.operator().equals("=");
  }

  // 条件の両辺が左テーブルの列またはリテラルだけで構成されていれば、結合前に評価できる
  private boolean canPushDownToLeft(
      Statement.Condition condition,
      Schema leftSchema,
      String leftTableName,
      Schema rightSchema,
      String rightTableName) {
    boolean leftOperandIsLeftColumn =
        isColumnOf(condition.left(), leftSchema, leftTableName)
            && !isColumnOf(condition.left(), rightSchema, rightTableName);

    boolean rightOperandIsJoinable = !isColumnOf(condition.right(), rightSchema, rightTableName);

    return leftOperandIsLeftColumn && rightOperandIsJoinable;
  }

  // トークンが指定したテーブルの列を指しているか
  private boolean isColumnOf(String token, Schema schema, String tableName) {
    int dot = token.indexOf('.');

    if (dot >= 0) {
      String qualifier = token.substring(0, dot);
      String columnName = token.substring(dot + 1);
      return qualifier.equalsIgnoreCase(tableName) && schema.getColumn(columnName) != null;
    }

    return schema.getColumn(token) != null;
  }

  private Operator createInsertPlan(Statement.Insert statement) {
    Table table = catalog.requireTable(statement.tableName());
    Schema schema = catalog.requireSchema(statement.tableName());
    return new InsertOperator(table, schema, statement);
  }

  private Operator createUpdatePlan(Statement.Update statement) {
    Table table = catalog.requireTable(statement.tableName());
    Schema schema = catalog.requireSchema(statement.tableName());
    return new UpdateOperator(table, schema, statement);
  }

  private Operator createDeletePlan(Statement.Delete statement) {
    Table table = catalog.requireTable(statement.tableName());
    return new DeleteOperator(table, statement);
  }
}
