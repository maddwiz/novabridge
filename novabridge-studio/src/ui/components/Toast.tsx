import { AnimatePresence, motion } from "framer-motion";

type ToastProps = {
  message: string;
  kind?: "info" | "error";
};

export function Toast({ message, kind = "info" }: ToastProps) {
  return (
    <AnimatePresence>
      {message ? (
        <motion.div
          initial={{ opacity: 0, y: 8 }}
          animate={{ opacity: 1, y: 0 }}
          exit={{ opacity: 0, y: 8 }}
          className={`fixed bottom-6 left-1/2 z-50 -translate-x-1/2 rounded-xl border px-3 py-2 text-sm ${kind === "error" ? "border-red-400/30 bg-red-500/20" : "border-sky-300/30 bg-sky-500/20"}`}
        >
          {message}
        </motion.div>
      ) : null}
    </AnimatePresence>
  );
}
