package customdb.chapter07.Executor;

import customdb.chapter07.DB.Catalog;
import customdb.chapter07.DB.Row;
import customdb.chapter07.DB.Schema;
import customdb.chapter07.Operator.Operator;
import customdb.chapter07.Parser.Statement;
import customdb.chapter07.Planner.Planner;
import java.io.IOException;
import java.nio.file.Path;

public class QueryExecutor {
  private final Catalog catalog;
  private final Planner planner;

  public QueryExecutor(Path baseDir) throws IOException {
    this.catalog = new Catalog(baseDir);
    this.planner = new Planner(this.catalog);
  }

  public void execute(Statement statement) throws IOException {
    if (statement instanceof Statement.CreateTable createTableStmt) {
      executeCreateTable(createTableStmt);
    } else {
      Operator rootPlan = planner.createPlan(statement);
      executePlan(rootPlan, statement);
    }
  }

  private void executeCreateTable(Statement.CreateTable statement) throws IOException {
    Schema schema = new Schema(statement.tableName(), statement.columns());
    catalog.createTable(schema);
    System.out.println("Table created: " + statement.tableName());
  }

  private void executePlan(Operator rootPlan, Statement statement) throws IOException {
    rootPlan.open();

    try {
      int count = 0;
      while (true) {
        Row row = rootPlan.next();
        if (row == null) {
          break;
        }
        if (statement instanceof Statement.Select) {
          System.out.println(row);
        } else if (statement instanceof Statement.Insert insertStmt) {
          System.out.println("Inserted into " + insertStmt.tableName() + ": " + row);
        }
        count++;
      }

      if (count == 0 && statement instanceof Statement.Select) {
        System.out.println("(empty)");
      } else if (statement instanceof Statement.Update) {
        System.out.println("Updated " + count + " row(s).");
      } else if (statement instanceof Statement.Delete) {
        System.out.println("Deleted " + count + " row(s).");
      }
    } finally {
      rootPlan.close();
    }
  }

  public void close() throws IOException {
    catalog.close();
  }
}
