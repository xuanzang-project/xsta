module alu8 (
    input  [7:0] a,
    input  [7:0] b,
    input  [2:0] op,
    output reg [7:0] y,
    output reg carry
);
    always @* begin
        carry = 1'b0;
        case (op)
            3'b000: {carry, y} = {1'b0, a} + {1'b0, b};
            3'b001: {carry, y} = {1'b0, a} - {1'b0, b};
            3'b010: y = a & b;
            3'b011: y = a | b;
            3'b100: y = a ^ b;
            3'b101: y = ~a;
            3'b110: y = a;
            default: y = 8'h00;
        endcase
    end
endmodule
