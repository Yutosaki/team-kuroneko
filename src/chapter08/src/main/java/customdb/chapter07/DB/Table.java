package customdb.chapter07.DB;

import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.LinkedList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Queue;

public class Table {
  private static final int PAGE_SIZE = 4096; // ページサイズを4KBに設定

  private final Schema schema;
  private final RandomAccessFile file;
  private final Map<String, Index> indexes = new HashMap<>();

  private final Queue<RecordId> freeList = new LinkedList<>();

  public record RecordId(int pageNo, int slotNo) {}

  public record Record(RecordId recordId, Row row) {}

  public Table(Schema schema, Path path) throws IOException {
    this.schema = schema;
    if (path.getParent() != null && !Files.exists(path.getParent())) {
      Files.createDirectories(path.getParent());
    }
    this.file = new RandomAccessFile(path.toFile(), "rw");
    // テーブル作成時にインデックスが定義されていれば構築する
    buildIndexes();
    buildFreeList();
  }

  public void truncate() throws IOException {
    file.setLength(0);
    indexes.clear();
    buildIndexes();
    freeList.clear();
  }

  public void insert(Row row) throws IOException {
    int recordSize = schema.getRecordSize();
    int maxSlots = schema.getMaxSlots(PAGE_SIZE);

    int numPages = (int) Math.ceil((double) file.length() / PAGE_SIZE);

    int targetPage;
    int targetSlot;

    if (!freeList.isEmpty()) {
      RecordId freeId = freeList.poll();
      targetPage = freeId.pageNo();
      targetSlot = freeId.slotNo();
    } else {
      targetPage = numPages;
      targetSlot = 0;
      for (int s = 1; s < maxSlots; s++) {
        freeList.offer(new RecordId(targetPage, s));
      }
    }

    byte[] page = new byte[PAGE_SIZE];

    if (targetPage < numPages) {
      file.seek((long) targetPage * PAGE_SIZE);
      file.read(page);
    }

    byte[] recordBytes = serialize(row);
    int offset = targetSlot * recordSize;

    System.arraycopy(recordBytes, 0, page, offset, recordSize);

    file.seek((long) targetPage * PAGE_SIZE);
    file.write(page);

    // インデックスに追加
    for (Map.Entry<String, Index> e : indexes.entrySet()) {
      String col = e.getKey();
      Index idx = e.getValue();
      idx.add(row.get(col), new RecordId(targetPage, targetSlot));
    }
  }

  public List<Row> scan() throws IOException {
    List<Row> rows = new ArrayList<>();

    for (Record record : scanRecords()) {
      rows.add(record.row());
    }

    return rows;
  }

  // 各レコードを物理位置（RecordId）付きで取得する
  public List<Record> scanRecords() throws IOException {
    List<Record> records = new ArrayList<>();

    int recordSize = schema.getRecordSize();
    int maxSlots = schema.getMaxSlots(PAGE_SIZE);
    int numPages = (int) Math.ceil((double) file.length() / PAGE_SIZE);

    for (int p = 0; p < numPages; p++) {
      byte[] page = new byte[PAGE_SIZE];
      file.seek((long) p * PAGE_SIZE);
      file.read(page);

      for (int s = 0; s < maxSlots; s++) {
        int offset = s * recordSize;

        if (page[offset] == 1) {
          byte[] recordBytes = Arrays.copyOfRange(page, offset, offset + recordSize);
          Row row = deserialize(recordBytes);

          if (row != null) {
            records.add(new Record(new RecordId(p, s), row));
          }
        }
      }
    }

    return records;
  }

  // 指定した物理位置のレコードを新しい内容で上書きする
  public void update(RecordId recordId, Row row) throws IOException {
    int recordSize = schema.getRecordSize();
    int offset = recordId.slotNo() * recordSize;

    byte[] page = new byte[PAGE_SIZE];

    file.seek((long) recordId.pageNo() * PAGE_SIZE);
    file.read(page);

    byte[] oldBytes = Arrays.copyOfRange(page, offset, offset + recordSize);
    Row oldRow = deserialize(oldBytes);

    byte[] recordBytes = serialize(row);
    System.arraycopy(recordBytes, 0, page, offset, recordSize);

    file.seek((long) recordId.pageNo() * PAGE_SIZE);
    file.write(page);

    if (oldRow != null) {
      for (Map.Entry<String, Index> e : indexes.entrySet()) {
        String col = e.getKey();
        Index idx = e.getValue();
        Object oldKey = oldRow.get(col);
        Object newKey = row.get(col);
        if (!Objects.equals(oldKey, newKey)) {
          idx.remove(oldKey, recordId);
          idx.add(newKey, recordId);
        }
      }
    }
  }

  public void delete(RecordId recordId) throws IOException {
    int recordSize = schema.getRecordSize();
    int offset = recordId.slotNo() * recordSize;

    byte[] page = new byte[PAGE_SIZE];

    file.seek((long) recordId.pageNo() * PAGE_SIZE);
    file.read(page);

    byte[] oldBytes = Arrays.copyOfRange(page, offset, offset + recordSize);
    Row oldRow = deserialize(oldBytes);

    page[offset] = 0;

    file.seek((long) recordId.pageNo() * PAGE_SIZE);
    file.write(page);

    if (oldRow != null) {
      for (Map.Entry<String, Index> e : indexes.entrySet()) {
        String col = e.getKey();
        Index idx = e.getValue();
        idx.remove(oldRow.get(col), recordId);
      }
    }

    freeList.offer(recordId);
  }

  // Row -> byte[]
  private byte[] serialize(Row row) {
    ByteBuffer bb = ByteBuffer.allocate(schema.getRecordSize());
    bb.put((byte) 1);

    for (Schema.Column column : schema.getColumns()) {
      Object value = row.get(column.name());

      switch (column.type()) {
        case INTEGER -> bb.putInt((Integer) value);
        case FLOAT -> bb.putFloat((Float) value);
        case DOUBLE -> bb.putDouble((Double) value);
        case STRING -> {
          byte[] bytes = value.toString().getBytes(StandardCharsets.UTF_8);
          bb.putInt(bytes.length);
          bb.put(bytes, 0, bytes.length);

          byte[] padding = new byte[column.length() - bytes.length];
          bb.put(padding);
        }
      }
    }

    return bb.array();
  }

  // byte[] -> Row
  private Row deserialize(byte[] recordBytes) {
    ByteBuffer bb = ByteBuffer.wrap(recordBytes);
    Row row = new Row();

    byte used = bb.get();
    if (used != 1) {
      return null;
    }

    for (Schema.Column column : schema.getColumns()) {
      switch (column.type()) {
        case INTEGER -> row.put(column.name(), bb.getInt());
        case FLOAT -> row.put(column.name(), bb.getFloat());
        case DOUBLE -> row.put(column.name(), bb.getDouble());
        case STRING -> {
          int len = bb.getInt();
          byte[] bytes = new byte[len];
          bb.get(bytes, 0, len);
          String value = new String(bytes, StandardCharsets.UTF_8);
          row.put(column.name(), value);

          int paddingLength = column.length() - len;
          if (paddingLength > 0) {
            bb.position(bb.position() + paddingLength);
          }
        }
      }
    }

    return row;
  }

  public void close() throws IOException {
    file.close();
  }

  private void buildIndexes() throws IOException {
    for (Schema.Column column : schema.getColumns()) {
      if (column.isIndexed()) {
        indexes.put(column.name(), new Index());
      }
    }

    if (indexes.isEmpty()) return;

    for (Record record : scanRecords()) {
      Row row = record.row();
      RecordId recordId = record.recordId();
      for (String col : indexes.keySet()) {
        Index idx = indexes.get(col);
        idx.add(row.get(col), recordId);
      }
    }
  }

  private void buildFreeList() throws IOException {
    freeList.clear();

    int recordSize = schema.getRecordSize();
    int maxSlots = schema.getMaxSlots(PAGE_SIZE);
    int numPages = (int) Math.ceil((double) file.length() / PAGE_SIZE);

    for (int p = 0; p < numPages; p++) {
      byte[] page = new byte[PAGE_SIZE];
      file.seek((long) p * PAGE_SIZE);
      file.read(page);

      for (int s = 0; s < maxSlots; s++) {
        int offset = s * recordSize;

        if (page[offset] == 0) {
          freeList.offer(new RecordId(p, s));
        }
      }
    }
  }

  public List<Row> searchByIndex(String column, Object value) throws IOException {
    Index idx = indexes.get(column);

    if (idx == null) {
      throw new IllegalStateException("No index found for column: " + column);
    }

    List<Row> rows = new ArrayList<>();
    for (RecordId recordId : idx.search(value)) {
      Row row = readRow(recordId);
      if (row != null) {
        rows.add(row);
      }
    }
    return rows;
  }

  private Row readRow(RecordId recordId) throws IOException {
    int recordSize = schema.getRecordSize();
    int offset = recordId.slotNo() * recordSize;

    byte[] page = new byte[PAGE_SIZE];
    file.seek((long) recordId.pageNo() * PAGE_SIZE);
    file.read(page);

    byte[] recordBytes = Arrays.copyOfRange(page, offset, offset + recordSize);
    return deserialize(recordBytes);
  }
}
