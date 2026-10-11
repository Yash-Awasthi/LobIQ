// Nasdaq TotalView-ITCH 5.0 parser for a BinaryFILE stream (2-byte big-endian length, then the
// message). Takes up to 8 bytes per clock and emits at most one message per clock, one clock
// after the beat that carries its last byte. Decodes the fields an order book needs (A F E C X D
// U P Q B); other types report the header only. Matches hft::itch::dispatch in src/feed/itch.hpp.
module itch_parser (
    input  logic        clk,
    input  logic        rst,
    input  logic        in_valid,
    input  logic [63:0] in_data,      // stream byte i in bits [8i+7:8i]
    input  logic [ 3:0] in_bytes,     // 1..8 bytes valid, from byte 0 up
    output logic        in_ready,
    output logic        out_valid,
    output logic [ 1:0] out_status,   // 0 ok, 1 truncated, 2 bad length, 3 unknown type
    output logic [ 7:0] out_type,
    output logic [15:0] out_locate,
    output logic [15:0] out_tracking,
    output logic [47:0] out_ts,
    output logic [63:0] out_ref,      // order reference; U: original reference
    output logic [63:0] out_new_ref,  // U only
    output logic [63:0] out_match,
    output logic [63:0] out_shares,   // 64-bit for Q
    output logic [31:0] out_price,
    output logic [ 7:0] out_side,
    output logic [63:0] out_stock,    // 8 ASCII bytes in stream order, first byte lowest
    output logic [ 7:0] out_flag      // C: printable, Q: cross type
);
  // Body fields start after the length (2 bytes) and the header (11 bytes).
  localparam int B = 13;

  // Holds at most one partial frame plus a beat: 51 + 8 < 64 (in_ready keeps it there).
  logic [511:0] buffer;
  logic [6:0] occ;
  logic [16:0] skip;  // bytes still to drop of an oversized frame

  function automatic logic [5:0] length_of(input logic [7:0] t);
    case (t)
      "S", "W": return 12;
      "R": return 39;
      "H": return 25;
      "Y", "N": return 20;
      "L": return 26;
      "V", "J", "U": return 35;
      "K": return 28;
      "h": return 21;
      "A", "C": return 36;
      "F", "Q": return 40;
      "E": return 31;
      "X": return 23;
      "D", "B": return 19;
      "P": return 44;
      "I": return 50;
      "O": return 48;
      default: return 0;
    endcase
  endfunction

  function automatic logic [63:0] be(input logic [511:0] b, input int at, input int n);
    logic [63:0] v = '0;
    for (int i = 0; i < n; i++) v = {v[55:0], b[(at+i)*8+:8]};
    return v;
  endfunction

  function automatic logic [7:0] byte_at(input logic [511:0] b, input int at);
    return b[at*8+:8];
  endfunction

  wire  [15:0] len = {buffer[7:0], buffer[15:8]};
  wire  [16:0] flen = 17'(len) + 17'd2;
  wire  [ 7:0] typ = buffer[23:16];
  wire  [ 5:0] want = length_of(typ);

  logic        emit;
  logic [ 6:0] drop;
  logic [16:0] skip_n;
  logic [ 1:0] status;

  always_comb begin
    emit = 0;
    drop = 0;
    skip_n = skip;
    status = 0;
    if (skip != 0) begin
      drop = skip > 17'(occ) ? occ : skip[6:0];
      skip_n = skip - 17'(drop);
    end else if (occ >= 2 && (17'(occ) >= flen || (len > 50 && occ >= 3))) begin
      // A frame longer than any message never fits: report it once the type byte is here, then skip.
      emit = 1;
      drop = 17'(occ) < flen ? occ : flen[6:0];
      skip_n = flen - 17'(drop);
      if (len == 0) status = 1;
      else if (want == 0) status = 3;
      else if (len < 16'(want)) status = 1;
      else if (len > 16'(want)) status = 2;
    end
  end

  wire  [6:0] kept = occ - drop;
  assign in_ready = kept <= 56;
  wire          take = in_valid && in_ready;
  wire  [ 63:0] keep = ~(64'hFFFF_FFFF_FFFF_FFFF << {in_bytes, 3'b0});
  wire  [511:0] incoming = 512'(in_data & keep);

  always_ff @(posedge clk) begin
    if (rst) begin
      buffer <= '0;
      occ <= 0;
      skip <= 0;
      out_valid <= 0;
    end else begin
      buffer <= (buffer >> {drop, 3'b0}) | (take ? incoming << {kept, 3'b0} : '0);
      occ <= kept + (take ? 7'(in_bytes) : 7'd0);
      skip <= skip_n;
      out_valid <= emit;
    end
  end

  always_ff @(posedge clk) begin
    out_status <= status;
    out_type <= typ;
    out_locate <= 16'(be(buffer, 3, 2));
    out_tracking <= 16'(be(buffer, 5, 2));
    out_ts <= 48'(be(buffer, 7, 6));
    out_ref <= '0;
    out_new_ref <= '0;
    out_match <= '0;
    out_shares <= '0;
    out_price <= '0;
    out_side <= '0;
    out_stock <= '0;
    out_flag <= '0;
    if (status == 0)
      case (typ)
        "A", "F", "P": begin
          out_ref <= be(buffer, B, 8);
          out_side <= byte_at(buffer, B + 8);
          out_shares <= be(buffer, B + 9, 4);
          out_stock <= buffer[(B+13)*8+:64];
          out_price <= 32'(be(buffer, B + 21, 4));
          if (typ == "P") out_match <= be(buffer, B + 25, 8);
        end
        "E", "C": begin
          out_ref <= be(buffer, B, 8);
          out_shares <= be(buffer, B + 8, 4);
          out_match <= be(buffer, B + 12, 8);
          if (typ == "C") begin
            out_flag  <= byte_at(buffer, B + 20);
            out_price <= 32'(be(buffer, B + 21, 4));
          end
        end
        "X": begin
          out_ref <= be(buffer, B, 8);
          out_shares <= be(buffer, B + 8, 4);
        end
        "D": out_ref <= be(buffer, B, 8);
        "U": begin
          out_ref <= be(buffer, B, 8);
          out_new_ref <= be(buffer, B + 8, 8);
          out_shares <= be(buffer, B + 16, 4);
          out_price <= 32'(be(buffer, B + 20, 4));
        end
        "Q": begin
          out_shares <= be(buffer, B, 8);
          out_stock <= buffer[(B+8)*8+:64];
          out_price <= 32'(be(buffer, B + 16, 4));
          out_match <= be(buffer, B + 20, 8);
          out_flag <= byte_at(buffer, B + 28);
        end
        "B": out_match <= be(buffer, B, 8);
        default: ;
      endcase
  end
endmodule
