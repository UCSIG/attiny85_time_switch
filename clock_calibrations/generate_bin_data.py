from dataclasses import dataclass
import os

# The normal clock frequency.
sleep_clock_hz = 128000

# Maximum deviation in positive direction of the sleep clock.
max_perc_sleep_clock = 25

# Maximum deviation in negative direction of the sleep clock.
min_perc_sleep_clock = max_perc_sleep_clock

# The line that the script detects as the table.
calibration_table_header = "Chip ID"

# The magic number that should be present so that the ic reads the clock calibration values.
calibration_magic_number = 0xCD

# Open file, read everything into list.
all_lines = []
with open("clock_calibrations.md", 'r') as f_handle:
    all_lines = f_handle.readlines()

@dataclass(unsafe_hash=True)
class table_item:
    id: int = 0
    frequency: int = 0


def main():
    calibration_header_detected = False
    table_data_row_count = -1
    data_rows = []
    for idx, line in enumerate(all_lines):

        # Actual data.
        if table_data_row_count >= 0 and line.count('|') == 4:
            data_row = table_item()
            line_split = line.split("|")

            data_row.id = int(line_split[1].replace(" ", ""), base=10)
            data_row.frequency = int(line_split[2].replace(" ", ""), base=10)
            data_rows.append(data_row)

            if data_row.frequency > ((100+max_perc_sleep_clock)*sleep_clock_hz/100) or data_row.frequency < ((100-min_perc_sleep_clock)*sleep_clock_hz/100):
                print(
                    f"Frequency {data_row.frequency}Hz of chip with id {data_row.id:08x} is bigger than specified limits, stopping here...")
                exit(1)

        if calibration_header_detected:
            table_data_row_count += 1

        if calibration_table_header in line:
            calibration_header_detected = True

    for chip in data_rows:
        clock_calibration_filename = f"clock_calibration_{chip.id:03.0f}.bin"

        binary_content = bytearray(chip.frequency.to_bytes(4, 'big'))
        binary_content.append(calibration_magic_number)

        if os.path.exists(clock_calibration_filename):
            print(f"{clock_calibration_filename} already exists, skipping...")
            continue

        with open(clock_calibration_filename, "wb+") as binary_f:
            binary_f.write(binary_content)

        print(f"Writing file '{clock_calibration_filename}'.")

if __name__ == "__main__":
    main()