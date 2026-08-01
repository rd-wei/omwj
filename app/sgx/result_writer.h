#ifndef EJ_RESULT_WRITER_H
#define EJ_RESULT_WRITER_H

/*
 * result_writer — host-side sink for ocall_stream_result.
 *
 * main() opens the writer with the output CSV path and column names before
 * the ecall; the ocall implementation appends one CSV line per result row.
 */

#include <string>
#include <vector>

void result_writer_open(const std::string& path,
                        const std::vector<std::string>& col_names);
void result_writer_close();

/* Number of rows written since open (for the summary printout). */
size_t result_writer_rows();

#endif /* EJ_RESULT_WRITER_H */
